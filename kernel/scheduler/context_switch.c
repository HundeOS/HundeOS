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
    bool command_flow_process = BString__equals(&process->name, &bstr("/usr/local/bin/bash"));
    if (command_flow_process)
    {
        __d_printf(COMMAND_FLOW, "t=%i dispatch pid=%i name=%s state=%s\n", now, process->pid, cstr(process->name),
                   ProcessState__to_string(process->thread.state));
    }
}
#else
static void ContextSwitch__trace_command_dispatch(ProcessPtr process, CPUTime now)
{
}
#endif

ContextSwitch ContextSwitch__new(ThreadPtr new_thread, OptionalPtr(ThreadPtr) old_thread)
{
    return (ContextSwitch){.new_thread = new_thread, .old_thread = *old_thread};
}

static bool Thread__finalise_deferred_syscalls(ThreadPtr self)
{
    ProcessPtr process = self->owner;
    while (true)
    {
        match_optional(
            Thread__pop_ready_parked_operation(self),
            some(parked_operation)
            {
                match_optional(
                    parked_operation->continuation,
                    some(continuation)
                    {
                        Process__switch_address_space(process, Thread__required_context(self),
                                                      ProcessPageTable__root(&process->address_space.page_table));

                        __d_printf(PROCESS_SWITCH, "Continuation calling %p %p", continuation, parked_operation);
                        Result(Anything) res = continuation(parked_operation);
                        if (Thread__is_parked(self))
                        {
                            ParkedOperation__dispose(parked_operation);
                            match_optional(
                                Thread__top_parked_operation_scheduler(self),
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
                                ThreadContext__set_syscall_result(Thread__required_context(self), result_val);
                            },
                            error(code, message)
                            {
                                ThreadContext__set_syscall_result(Thread__required_context(self), code);
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
        self->old_thread,
        some(old_thread)
        {
            ProcessPtr proc = old_thread->owner;
            CPUTime now = CPU__time_usec();
            CPUUsage__stop(&proc->accounting.cpu_usage, now, Process__user_wait_usec(proc, now));
            if (old_thread->state != PROC_STATE_DEAD && old_thread->state != PROC_STATE_WAITING &&
                old_thread->state != PROC_STATE_SLEEPING && old_thread->state != PROC_STATE_STOPPED)
            {
                old_thread->state = PROC_STATE_INTERRUPTED;
            }
        },
        none_do_nothing()
    );

    ThreadPtr new_thread = self->new_thread;
    ProcessPtr new_proc = new_thread->owner;
    new_thread->migration_safe = false;
    QueueWaitUsage__select(&new_thread->queue_wait_usage);
    CPUTime now = CPU__time_usec();
    CPUUsage__start_mode(&new_proc->accounting.cpu_usage, now, Process__user_wait_usec(new_proc, now),
                         CPU_USAGE_PAUSED);
    ContextSwitch__trace_command_dispatch(new_proc, now);
    __d_printf(PROCESS_SWITCH, "ContextSwitch__switch target pid=%i name=%s state=%s context=%p page_table=%p\n",
               new_proc->pid, cstr(new_proc->name), ProcessState__to_string(new_thread->state),
               Thread__required_context(new_thread),
               paddr(ProcessPageTable__root(&new_proc->address_space.page_table)));
    __d_printf(PROCESS_SWITCH, "Switch to proc: %i %s - %s.\n", new_proc->pid, cstr(new_proc->name),
               ProcessState__to_string(new_thread->state));

    new_thread->state = PROC_STATE_RUNNING;
    Process__account_cpu_mode(new_proc, CPU_USAGE_SYSTEM);
    if (Process__has_pending_kill(new_proc) &&
        Scheduler__deliver_pending_signal(new_proc->kernel->scheduler, new_thread,
                                          Thread__required_context(new_thread)) == PROCESS_SIGNAL_DELIVERY_TERMINATE)
    {
        return;
    }
    if (!Thread__finalise_deferred_syscalls(new_thread))
    {
        return;
    }
    ProcessSignalDeliveryKind delivery = Scheduler__deliver_pending_signal(new_proc->kernel->scheduler, new_thread,
                                                                           Thread__required_context(new_thread));
    if (delivery == PROCESS_SIGNAL_DELIVERY_TERMINATE)
    {
        return;
    }
    if (delivery == PROCESS_SIGNAL_DELIVERY_STOP)
    {
        Process__switch_address_space(new_proc, Thread__required_context(new_thread),
                                      ProcessPageTable__root(&new_proc->address_space.page_table));
        Scheduler__yield_waiting(new_proc->kernel->scheduler, new_thread, CPU__id());
        return;
    }
    if (Thread__is_parked(new_thread))
    {
        Process__switch_address_space(new_proc, Thread__required_context(new_thread),
                                      ProcessPageTable__root(&new_proc->address_space.page_table));
        match_optional(
            Thread__top_park_reason(new_thread),
            some(reason)
            {
                new_thread->state = reason == PARK_SLEEP ? PROC_STATE_SLEEPING : PROC_STATE_WAITING;
            },
            none_do_nothing()
        );
        Scheduler__yield_waiting(new_proc->kernel->scheduler, new_thread, CPU__id());
        return;
    }

    new_thread->dispatched_at = CPU__time_usec();
    match_optional(
        new_thread->preferred_ticket,
        some(ticket)
        {
            if (new_thread->preferred_trace_pending)
            {
                new_thread->preferred_dispatched_at_usec = new_thread->dispatched_at;
                Process__publish_dispatch_trace(new_proc, &ticket, new_thread->preferred_eligible_at_usec,
                                                new_thread->preferred_selected_at_usec,
                                                new_thread->preferred_dispatched_at_usec);
                new_thread->preferred_trace_pending = false;
            }
        },
        none_do_nothing()
    );
    Process__account_cpu_mode(new_proc, CPU_USAGE_USER);
    CPUTimer__resume();
    Process__switch_context(new_proc, Thread__required_context(new_thread),
                            ProcessPageTable__root(&new_proc->address_space.page_table));
}
