#include "scheduler.h"
#include "../types/result.h"
#include "../types/optional.h"
#include "../mutex.h"
#include "parked.h"

FixedArrayBodyDefine(CPUSchedulers);

#define CPU_SCHEDULER_INIT(idle, cpu) \
    (CPUScheduler) { \
        .idle_process = idle, \
        .processes = EmptyArray(ProcessTable), \
        .scheduler_lock = MUTEX_INIT, \
        .last_tick = 0, \
        .cpu = cpu, \
    }

Result(CPUSchedulerPtr) CPUScheduler__heap_new(ElfPtr idle_process, CPUID cpu)
{
    ResultMatch(ProcessPtr, Process__setup(idle_process))
        IsSuccess(idle,
        {
            CPUSchedulerPtr sched = v_ptr(Kernel__alloc(sizeof(CPUScheduler)), CPUScheduler);
            *sched = CPU_SCHEDULER_INIT(idle, cpu);
            return Success(CPUSchedulerPtr, sched);
        });
        IsError(code, message, return Error(CPUSchedulerPtr, message, code));
    End
}

bool process_in_state(ProcessPtr * proc, void * compare_state)
{
    ProcessState state = (*proc)->state;
    ProcessState compare = *(ProcessState *) compare_state;
    __d_printf(SCHEDULER, "process_in_state %s %s\n", cstr((*proc)->name), ProcessState__to_string(state));

    return state & compare;
}

Result(CPUID) CPUScheduler__schedule(CPUSchedulerPtr self, ProcessPtr new_proc)
{
    __d_printf(SCHEDULER, "Queuing %s %s\n", cstr(new_proc->name), ProcessState__to_string(new_proc->state));
    CriticalSection(self->scheduler_lock,
    {
        new_proc->state = PROC_STATE_READY;
        __d_printf(SCHEDULER, "%s %s\n", cstr(new_proc->name), ProcessState__to_string(new_proc->state));
        ProcessTable__push(&self->processes, new_proc);
        _cs_return Success(CPUID, self->cpu);
    });
}

int longest_waiting(ProcessPtr * proc1, ProcessPtr * proc2)
{
    return (*proc2)->last_tick - (*proc1)->last_tick;
}

Optional(ProcessPtr) CPUScheduler__current_process(CPUSchedulerPtr self)
{
    ProcessState running_state = PROC_STATE_RUNNING;
    FindResultOptional(ProcessTable) current_match = ProcessTable__find(&self->processes, process_in_state, &running_state);

    FindResultMatch(ProcessTable, current_match)
        IsSome(match,
        {
            return Some(ProcessPtr, *match.value);
        })
        IsNone(
        {
            if (self->idle_process->state == PROC_STATE_RUNNING)
            {
                return Some(ProcessPtr, self->idle_process);
            }
            return None(ProcessPtr);
        })
    End

    return None(ProcessPtr);
}

bool CPUScheduler__is_idle(CPUSchedulerPtr self)
{
    ProcessState running_state = PROC_STATE_RUNNING;
    FindResultOptional(ProcessTable) current_match = ProcessTable__find(&self->processes, process_in_state, &running_state);

    FindResultMatch(ProcessTable, current_match)
        IsSome(match,
        {
            return false;
        })
        IsNone(
        {
            return true;
        })
    End

    return true;
}

Result(ProcessPtr) CPUScheduler__exit(CPUSchedulerPtr self, ProcessPtr proc)
{
    __d_printf(SCHEDULER, "CPUScheduler__exit %s\n", cstr(proc->name));
    CriticalSection(self->scheduler_lock,
    {
        Optional(ProcessPtr) current = CPUScheduler__current_process(self);
        ResultMatch(ArrayCode, ProcessTable__remove(&self->processes, proc))
            IsError(code, message,
            {
                _cs_return Error(ProcessPtr, kstring("process not found"), SCHEDULER_PROCESS_NOT_FOUND);
            })
            IsSuccess(process_index,
            {
                proc->state = PROC_STATE_DEAD;
                _cs_return Success(ProcessPtr, proc);
            })
        End
    });
}

Result(ProcessPtr) CPUScheduler__park(
        CPUSchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context, Optional(Continuation) kernel_continuation
    )
{
    __d_printf(SCHEDULER, "CPUScheduler__park %s\n", cstr(proc->name));
    CriticalSection(self->scheduler_lock,
    {
        Match(ArrayIndex, ProcessTable__find_item(&self->processes, proc))
            IsSome(index,
            {
                ParkedKernelContextPtr parked = ParkedKernelContext__heap_new(kernel_context, kernel_continuation);
                proc->state = PROC_STATE_WAITING;
                proc->parked_context = Some(ParkedKernelContextPtr, parked);
                _cs_return Success(ProcessPtr, proc);
            })
            IsNone(
            {
                _cs_return Error(ProcessPtr, kstring("process not found"), SCHEDULER_PROCESS_NOT_FOUND);
            })
        End
    });
}

Result(ProcessPtr) CPUScheduler__unpark(CPUSchedulerPtr self, ProcessPtr proc)
{
    __d_printf(SCHEDULER, "CPUScheduler__unpark %s\n", cstr(proc->name));
    CriticalSection(self->scheduler_lock,
    {
        Match(ArrayIndex, ProcessTable__find_item(&self->processes, proc))
            IsSome(index,
            {
                __d_printf(SCHEDULER, "CPUScheduler__unpark - done %s\n", cstr(proc->name));
                proc->state = PROC_STATE_INTERRUPTED;
                _cs_return Success(ProcessPtr, proc);
            })
            IsNone(
            {
                _cs_return Error(ProcessPtr, kstring("process not found"), SCHEDULER_PROCESS_NOT_FOUND);
            })
        End
    });
}

Optional(ContextSwitch) CPUScheduler__interrupt(CPUSchedulerPtr self, ProcessContextPtr context)
{
    ProcessState ready_state = PROC_STATE_READY | PROC_STATE_INTERRUPTED;

    CriticalSection(self->scheduler_lock,
    {
        Optional(ProcessPtr) current = CPUScheduler__current_process(self);
        self->last_tick++;

        Match(ProcessPtr, current)
            IsSome(proc,
            {
                __d_printf(SCHEDULER, "CPUScheduler__interrupt %s\n", cstr(proc->name));
                Process__save_context(proc, context);
                proc->last_tick = self->last_tick;
                proc->state = PROC_STATE_SWITCHING_CONTEXT;
            })
        End

         __d_println(SCHEDULER, "CPUScheduler__interrupt\n");

        ProcessTable ready = ProcessTable__filter(&self->processes, process_in_state, &ready_state);
        ProcessTable sorted_ready = ProcessTable__sort_desc(&ready, longest_waiting);

        __d_printf(SCHEDULER, "total num processes - %i\n", ProcessTable__len(&self->processes));
        __d_printf(SCHEDULER, "num ready - %i\n", ProcessTable__len(&sorted_ready));

        Optional(ProcessPtr) selected = ProcessTable__at(&sorted_ready, 0);
        Match(ProcessPtr, selected)
            IsSome(new_proc,
            {
                __d_printf(SCHEDULER, "selected - %p %s\n", new_proc, cstr(new_proc->name));
                new_proc->state = PROC_STATE_SWITCHING_CONTEXT;
                ContextSwitch context_switch = ContextSwitch__new(new_proc, current);

                _cs_return Some(ContextSwitch, context_switch);
            });
            IsNone({
                Match(ProcessPtr, current)
                    IsSome(proc,
                    {
                        // we have a current process context
                        // so restore current to running
                        proc->state = PROC_STATE_RUNNING;
                        __d_printf(SCHEDULER, "no new proc remain with current - %p %s\n", proc, cstr(proc->name));
                        _cs_return None(ContextSwitch);
                    });
                    IsNone(
                    {
                        // no current and nothing ready, go idle
                        ProcessPtr idle = self->idle_process;
                        idle->state = PROC_STATE_SWITCHING_CONTEXT;
                        __d_printf(SCHEDULER, "no new proc or current going idle - %p %s\n", idle, cstr(idle->name));
                        ContextSwitch context_switch = ContextSwitch__new(idle, current);
                        _cs_return Some(ContextSwitch, context_switch);
                    });
                End

            })
        End
    });
    return None(ContextSwitch);
}

// __attribute__((noreturn))
void CPUScheduler__start(CPUSchedulerPtr self)
{
    ContextSwitch context_switch = ContextSwitch__new(self->idle_process, None(ProcessPtr));
    ContextSwitch__switch(Some(ContextSwitch, context_switch));
}

ProcessTablePtr Scheduler__all_processes(SchedulerPtr self, CPUID cpu)
{
    Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
        IsSome(scheduler,
        {
            return &scheduler->processes;
        });
    End
}

Result(ProcessPtr) Scheduler__exit(SchedulerPtr self, ProcessPtr proc)
{
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        Result(ProcessPtr) res = CPUScheduler__exit(sched, proc);
        ResultMatch(ProcessPtr, res)
            IsSuccess(exited,
            {
                Optional(ProcessPtr) current_for_cpu = CPUScheduler__current_process(sched);

                Match(ProcessPtr, current_for_cpu)
                    IsNone(Scheduler__preempt(self, cpu, NULL));
                End

                return res;
            });
        End
    End


    KString no_scheduler_format = kstring("Process not found: %i\n");
    KString no_scheduler = KString__sprintf(&no_scheduler_format, proc->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}


Result(ProcessPtr) Scheduler__park(
        SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context, Optional(Continuation) kernel_continuation
    )
{
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        Result(ProcessPtr) res = CPUScheduler__park(sched, proc, kernel_context, kernel_continuation);
        ResultMatch(ProcessPtr, res)
            IsSuccess(exited,
            {
                Optional(ProcessPtr) current_for_cpu = CPUScheduler__current_process(sched);

                Match(ProcessPtr, current_for_cpu)
                    IsNone(Scheduler__preempt(self, cpu, NULL));
                End

                return res;
            });
        End
    End


    KString no_scheduler_format = kstring("No scheduler found for process: %i\n");
    KString no_scheduler = KString__sprintf(&no_scheduler_format, proc->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ProcessPtr) Scheduler__unpark(SchedulerPtr self, ProcessPtr proc)
{
    __d_println(SCHEDULER, "scheduler unparking");
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        Result(ProcessPtr) res = CPUScheduler__unpark(sched, proc);
        ResultMatch(ProcessPtr, res)
            IsSuccess(exited,
            {
                __d_println(SCHEDULER, "scheduler unparked process");
                return res;
            });
        End
    End


    KString no_scheduler_format = kstring("Process not found: %i\n");
    KString no_scheduler = KString__sprintf(&no_scheduler_format, proc->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}


Result(SchedulerInitStatus) Scheduler__init(SchedulerPtr self, ElfPtr idle_process)
{
    __d_println(SCHEDULER, "Scheduler__init");
    for (int i = 0; i < CPU__count(); i++)
    {
        __d_printf(SCHEDULER, "Scheduler__init cpu - %i\n", i);
        ResultMatch(CPUSchedulerPtr, CPUScheduler__heap_new(idle_process, i))
            IsError(code, message,
            {
                return Error(SchedulerInitStatus, message, SCHEDULER_INIT_ERROR);
            });
            IsSuccess(cpu_scheduler,
            {
                __d_printf(SCHEDULER, "Scheduler__init adding cpu sched - %i\n", i);
                CPUSchedulers__push(&self->schedulers, cpu_scheduler);
            });
        End
    }
    return Success(SchedulerInitStatus, SCHEDULER_INIT_OK);
}

Optional(ProcessPtr) Scheduler__current_process(SchedulerPtr self, CPUID cpu)
{
    CriticalSection(self->scheduler_lock,
    {
        Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
            IsSome(scheduler,
            {
                _cs_return CPUScheduler__current_process(scheduler);
            });
        End
    });
    return None(ProcessPtr);
}

Optional(ProcessPtr) Scheduler__save_context(SchedulerPtr self, CPUID cpu, ProcessContextPtr context)
{
    Match(ProcessPtr, Scheduler__current_process(self, cpu))
        IsSome(proc,
        {
            __d_printf(SCHEDULER, "Scheduler__save_context %s\n", cstr(proc->name));
            Process__save_context(proc, context);
            return Some(ProcessPtr, proc);
        });
    End
    return None(ProcessPtr);
}

Optional(ProcessPtr) Scheduler__idle_process(SchedulerPtr self, CPUID cpu)
{
    Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
        IsSome(scheduler, return Some(ProcessPtr, scheduler->idle_process));
    End
    return None(ProcessPtr);
}

SchedulerPtr Scheduler__heap_new()
{
    SchedulerPtr sched = v_ptr(Kernel__alloc(sizeof(Scheduler)), Scheduler);
    *sched = NEW_SCHEDULER;
    return sched;
}

Result(CPUID) Scheduler__schedule(SchedulerPtr self, ProcessPtr new_proc)
{
    // just round robin the cpus
    ProcessID pid = new_proc->pid;
    CPUID cpu = (pid % CPU__count());
    Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
        IsSome(scheduler,
        {
            return CPUScheduler__schedule(scheduler, new_proc);
        })
        IsNone(
        {
            KString invalid_cpu_format = kstring("Invalid CPU %i\n");
            KString invalid_cpu = KString__sprintf(&invalid_cpu_format, cpu);

            return Error(CPUID, invalid_cpu, SCHEDULER_INVALID_CPU);
        });
    End

    KString unreachable_code = kstring("Unreachable code reached");
    return Error(CPUID, unreachable_code, SCHEDULER_GENERAL_ERROR);
}


Optional(ContextSwitch) Scheduler__interrupt(SchedulerPtr self, CPUID cpu, ProcessContextPtr context)
{
    CriticalSection(self->scheduler_lock,
    {
        if (!self->started)
        {
            _cs_return None(ContextSwitch);
        }
        Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
            IsSome(scheduler,
            {
                __d_printf(SCHEDULER, "CPUScheduler__interrupt %i\n", cpu);
                _cs_return CPUScheduler__interrupt(scheduler, context);
            });
            IsNone(
            {
                __d_printf(PANIC, "Invalid CPU %i\n", cpu);
                _cs_return None(ContextSwitch);
            });
        End

    });

    return None(ContextSwitch);
}


void Scheduler__preempt(SchedulerPtr self, CPUID cpu, ProcessContextPtr context)
{
    Optional(ContextSwitch) context_switch = Scheduler__interrupt(self, cpu, context);
    ContextSwitch__switch(context_switch);
}


Result(SchedulerInitStatus) Scheduler__start(SchedulerPtr self, CPUID cpu)
{
    // we just spawn the idle process and new process starts will rely on the queues
    CriticalSection(self->scheduler_lock,
    {
        Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
            IsSome(scheduler,
            {
                // will never return from here
                self->started = true;
                _cs_exit_then(CPUScheduler__start(scheduler));
            });
            IsNone(
            {
                KString invalid_cpu_format = kstring("Invalid CPU %i\n");
                KString invalid_cpu = KString__sprintf(&invalid_cpu_format, cpu);

                _cs_return Error(SchedulerInitStatus, invalid_cpu, SCHEDULER_INVALID_CPU);
                __d_println(MUTEX, "Unreachable code reached");
            });
        End
    });

    KString scheduler_failed_format = kstring("Scheduler failed to start CPU %i\n");

    return Error(SchedulerInitStatus, KString__sprintf(&scheduler_failed_format, cpu), SCHEDULER_INIT_ERROR);
}
