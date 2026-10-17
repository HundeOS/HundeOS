#include "context_switch.h"
#include "../process.h"
#include "../arch/process.h"
#include "../arch/cpu_timer.h"
#include "../debug.h"
#include "scheduler/scheduler.h"
#include "../kernel_core.h"
#include "shared_wait.h"

#ifdef KERNEL_COMMAND_TRACE
static void ContextSwitch__trace_command_dispatch(ProcessPtr process, CPUTime now)
{
    bool command_flow_process = BString__equals(&process->name, &bstr("/usr/local/bin/bash")) ||
                                BString__equals(&process->name, &bstr("/sbin/drivers/uart_rx")) ||
                                BString__equals(&process->name, &bstr("/sbin/drivers/uart_tx"));
    if (command_flow_process)
    {
        __d_printf(COMMAND_FLOW, "t=%i dispatch pid=%i name=%s state=%s\n", now, process->pid, cstr(process->name),
                   ProcessState__to_string(process->state));
    }
}
#else
static void ContextSwitch__trace_command_dispatch(ProcessPtr process, CPUTime now)
{
}
#endif

ContextSwitch ContextSwitch__new(ProcessPtr new_proc, OptionalPtr(ProcessPtr) old_proc)
{
    return (ContextSwitch){.new_proc = new_proc, .old_proc = *old_proc};
}

static bool Process__finalise_deferred_syscalls(ProcessPtr self)
{
    while (true)
    {
        match_optional(
            Process__pop_ready_parked_operation(self),
            some(parked_operation)
            {
                match_optional(
                    parked_operation->continuation,
                    some(continuation)
                    {
                        Process__activate_address_space(self);

                        __d_printf(PROCESS_SWITCH, "Continuation calling %p %p", continuation, parked_operation);
                        Result(Anything) res = continuation(parked_operation);
                        if (Process__is_parked(self))
                        {
                            ParkedOperation__dispose(parked_operation);
                            match_optional(
                                Process__top_parked_operation_scheduler(self),
                                some(scheduler)
                                {
                                    Scheduler__yield_waiting(scheduler, self, CPU__id());
                                },
                                none_do_nothing()
                            );
                            return false;
                        }

                        match_result(
                            res,
                            success(result_val)
                            {
                                __d_println(PROCESS, "Set syscall result");
                                Process__set_syscall_result(self, result_val);
                            },
                            error(code, message)
                            {
                                Process__set_syscall_result(self, code);
                                __d_printf(PANIC, "Continuation error: %s\n", cstr(message));
                            }
                        );

                        ParkedOperation__dispose(parked_operation);
                    },
                    none()
                    {
                        ParkedOperation__dispose(parked_operation);
                    }
                );
            },
            none()
            {
                return true;
            }
        );
    }
}

void ContextSwitch__switch(ConstContextSwitchPtr self)
{
    match_optional(
        self->old_proc,
        some(proc)
        {
            CPUTime now = CPU__time_usec();
            CPUUsage__stop(&proc->cpu_usage, now, Process__user_wait_usec(proc, now));
            if (proc->state != PROC_STATE_DEAD && proc->state != PROC_STATE_WAITING &&
                proc->state != PROC_STATE_SLEEPING && proc->state != PROC_STATE_STOPPED)
            {
                proc->state = PROC_STATE_INTERRUPTED;
            }
        },
        none_do_nothing()
    );

    ProcessPtr new_proc = self->new_proc;
    new_proc->migration_safe = false;
    QueueWaitUsage__select(&new_proc->queue_wait_usage);
    CPUTime now = CPU__time_usec();
    CPUUsage__start_mode(&new_proc->cpu_usage, now, Process__user_wait_usec(new_proc, now), CPU_USAGE_PAUSED);
    ContextSwitch__trace_command_dispatch(new_proc, now);
    __d_printf(PROCESS_SWITCH, "ContextSwitch__switch target pid=%i name=%s state=%s context=%p page_table=%p\n",
               new_proc->pid, cstr(new_proc->name), ProcessState__to_string(new_proc->state),
               Process__required_context(new_proc), paddr(ProcessPageTable__root(&new_proc->page_table)));
    __d_printf(PROCESS_SWITCH, "Switch to proc: %i %s - %s.\n", new_proc->pid, cstr(new_proc->name),
               ProcessState__to_string(new_proc->state));

    new_proc->state = PROC_STATE_RUNNING;
    Process__account_cpu_mode(new_proc, CPU_USAGE_SYSTEM);
    if (Process__has_pending_kill(new_proc) &&
        Scheduler__deliver_pending_signal(new_proc->kernel->scheduler, new_proc, Process__required_context(new_proc)) ==
            PROCESS_SIGNAL_DELIVERY_TERMINATE)
    {
        return;
    }
    if (!Process__finalise_deferred_syscalls(new_proc))
    {
        return;
    }
    ProcessSignalDeliveryKind delivery =
        Scheduler__deliver_pending_signal(new_proc->kernel->scheduler, new_proc, Process__required_context(new_proc));
    if (delivery == PROCESS_SIGNAL_DELIVERY_TERMINATE)
    {
        return;
    }
    if (delivery == PROCESS_SIGNAL_DELIVERY_STOP)
    {
        Scheduler__yield_waiting(new_proc->kernel->scheduler, new_proc, CPU__id());
        return;
    }
    if (Process__is_parked(new_proc))
    {
        match_optional(
            Process__top_park_reason(new_proc),
            some(reason)
            {
                new_proc->state = reason == PARK_SLEEP ? PROC_STATE_SLEEPING : PROC_STATE_WAITING;
            },
            none_do_nothing()
        );
        Scheduler__yield_waiting(new_proc->kernel->scheduler, new_proc, CPU__id());
        return;
    }

    new_proc->dispatched_at = CPU__time_usec();
    match_optional(
        new_proc->preferred_ticket,
        some(ticket)
        {
            if (new_proc->preferred_trace_pending)
            {
                new_proc->preferred_dispatched_at_usec = new_proc->dispatched_at;
                Process__publish_dispatch_trace(new_proc, &ticket, new_proc->preferred_eligible_at_usec,
                                                new_proc->preferred_selected_at_usec,
                                                new_proc->preferred_dispatched_at_usec);
                new_proc->preferred_trace_pending = false;
            }
        },
        none_do_nothing()
    );
    Process__account_cpu_mode(new_proc, CPU_USAGE_USER);
    CPUTimer__resume();
    Process__context_switch(new_proc);
}
