#include "scheduler.h"
#include "../types/result.h"

FixedArrayBodyDefine(CPUSchedulers);
FixedArrayBodyDefine(ProcessQueue);

#define CPU_SCHEDULER_INIT(idle) \
    (CPUScheduler) { \
        .idle_process = idle, \
        .ready_processes = EmptyArray(ProcessQueue), \
        .waiting_processes = EmptyArray(ProcessQueue), \
        .scheduler_lock = MUTEX_INIT, \
    }

Result(CPUSchedulerPtr) CPUScheduler__heap_new(ElfPtr idle_process)
{
    ResultMatch(ProcessPtr, Process__setup(idle_process))
        IsSuccess(idle,
        {
            CPUSchedulerPtr sched = v_ptr(Kernel__alloc(sizeof(CPUScheduler)), CPUScheduler);
            *sched = CPU_SCHEDULER_INIT(idle);
            return Success(CPUSchedulerPtr, sched);
        });
        IsError(code, message, return Error(CPUSchedulerPtr, message, code));
    End
}

Result(SchedulerInitStatus) Scheduler__init(SchedulerPtr self, ElfPtr idle_process)
{
    __d_println(FOCUS, "Scheduler__init");
    for (int i = 0; i < CPU__count(); i++)
    {
        __d_printf(FOCUS, "Scheduler__init cpu - %i\n", i);
        ResultMatch(CPUSchedulerPtr, CPUScheduler__heap_new(idle_process))
            IsError(code, message,
            {
                return Error(SchedulerInitStatus, message, SCHEDULER_INIT_ERROR);
            });
            IsSuccess(cpu_scheduler,
            {
                __d_printf(FOCUS, "Scheduler__init adding cpu sched - %i\n", i);
                CPUSchedulers__push(&self->schedulers, cpu_scheduler);
            });
        End
    }
    return Success(SchedulerInitStatus, SCHEDULER_INIT_OK);
}