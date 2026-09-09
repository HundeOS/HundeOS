#include "context_switch.h"
#include "../process.h"
#include "../arch/process.h"
#include "../debug.h"
#include "scheduler/scheduler.h"
#include "../kernel_core.h"

ContextSwitch ContextSwitch__new(ProcessPtr new_proc, OptionalPtr(ProcessPtr) old_proc)
{
    return (ContextSwitch){.new_proc = new_proc, .old_proc = *old_proc};
}

static Result(Anything) Process__finalise_deferred_syscalls(ProcessPtr self)
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
                            match_optional(
                                Process__top_parked_operation_scheduler(self),
                                some(scheduler) { Scheduler__yield_waiting(scheduler, self, CPU__id() - 1); },
                                none_do_nothing()
                            );
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
                    none() { ParkedOperation__dispose(parked_operation); }
                );
            },
            none() { return Success(Anything, 0); }
        );
    }
}

void ContextSwitch__switch(const ContextSwitch * self)
{
    match_optional(
        self->old_proc,
        some(proc)
        {
            if (proc->state != PROC_STATE_DEAD && proc->state != PROC_STATE_WAITING &&
                proc->state != PROC_STATE_SLEEPING)
            {
                proc->state = PROC_STATE_INTERRUPTED;
            }
        },
        none_do_nothing()
    );

    ProcessPtr new_proc = self->new_proc;
    __d_printf(PROCESS_SWITCH, "ContextSwitch__switch target pid=%i name=%s state=%s context=%p page_table=%p\n",
               new_proc->pid, cstr(new_proc->name), ProcessState__to_string(new_proc->state), new_proc->context,
               paddr(ProcessPageTable__root(&new_proc->page_table)));
    __d_printf(PROCESS_SWITCH, "Switch to proc: %i %s - %s.\n", new_proc->pid, cstr(new_proc->name),
               ProcessState__to_string(new_proc->state));

    new_proc->state = PROC_STATE_RUNNING;
    match_optional(
        Process__take_pending_signal(new_proc),
        some(signal)
        {
            match_result(
                Process__dispose_owned_kernel_resources(new_proc),
                success_do_nothing(),
                error(code, message)
                {
                    __d_printf(PANIC, "Signal resource cleanup failed: %s\n", cstr(message));
                    CPU__halt_all();
                    return;
                }
            );
            match_result(
                Scheduler__exit(new_proc->kernel->scheduler, new_proc, ProcessSignal__exit_code(signal)),
                success_do_nothing(),
                error(code, message)
                {
                    __d_printf(PANIC, "Signal termination failed: %s\n", cstr(message));
                    CPU__halt_all();
                }
            );
            return;
        },
        none_do_nothing()
    );
    Process__finalise_deferred_syscalls(new_proc);

    Process__context_switch(new_proc);
}
