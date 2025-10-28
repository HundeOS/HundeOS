#ifndef __KERNEL_SCHEDULER_SCHEDULER_H
#define __KERNEL_SCHEDULER_SCHEDULER_H

#include <stdint.h>

#include "../types/types.h"
#include "../types/result.h"
#include "../types/fixed_array.h"
#include "../process.h"

FixedArrayDefine(ProcessPtr, MAX_PROCESSES, ProcessQueue);

typedef struct CPUScheduler
{
    ProcessQueue waiting_processes;
    ProcessQueue ready_processes;
    ProcessPtr idle_process;
    mutex_t scheduler_lock;
} CPUScheduler;

typedef CPUScheduler * CPUSchedulerPtr;

OptionalDef(CPUSchedulerPtr);
ResultDef(CPUSchedulerPtr);

FixedArrayDefine(CPUSchedulerPtr, MAX_CPUS, CPUSchedulers);

typedef struct Scheduler
{
    CPUSchedulers schedulers;
} Scheduler;


#endif