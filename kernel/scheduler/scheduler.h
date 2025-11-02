#ifndef __KERNEL_SCHEDULER_SCHEDULER_H
#define __KERNEL_SCHEDULER_SCHEDULER_H

#include <stdint.h>

#include "../types/types.h"
#include "../types/result.h"
#include "../types/fixed_array.h"
#include "../process.h"

FixedArrayDefine(ProcessPtr, MAX_PROCESSES, ProcessQueue, &Ptr__equals);

typedef uint64_t SchedulerInitStatus;
ResultDef(SchedulerInitStatus);

typedef enum SchedulerInitStatuses
{
    SCHEDULER_INIT_OK = 0,
    SCHEDULER_INIT_ERROR = -1,
} SchedulerInitStatuses;

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

FixedArrayDefine(CPUSchedulerPtr, MAX_CPUS, CPUSchedulers, &Ptr__equals);

typedef struct Scheduler
{
    CPUSchedulers schedulers;
} Scheduler;

typedef Scheduler * SchedulerPtr;

OptionalDef(SchedulerPtr);
ResultDef(SchedulerPtr);

Result(SchedulerInitStatus) Scheduler__init(SchedulerPtr self, ElfPtr idle_process);

#define NEW_SCHEDULER \
    (Scheduler) { \
        .schedulers = EmptyArray(CPUSchedulers)\
    } \

#endif