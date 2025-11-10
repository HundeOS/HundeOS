#ifndef __KERNEL_SCHEDULER_SCHEDULER_H
#define __KERNEL_SCHEDULER_SCHEDULER_H

#include <stdint.h>

#include "../types/types.h"
#include "../types/result.h"
#include "../types/fixed_array.h"
#include "../process.h"
#include "context.h"

typedef uint64_t SchedulerInitStatus;

ResultDef(SchedulerInitStatus);

typedef enum SchedulerInitStatuses
{
    SCHEDULER_INIT_OK = 0,
    SCHEDULER_INIT_ERROR = -1,
} SchedulerInitStatuses;

typedef enum SchedulerErrors
{
    SCHEDULER_INVALID_CPU = -1,
    SCHEDULER_GENERAL_ERROR = -2,
    SCHEDULER_NOT_STARTED = -3
} SchedulerErrors;

typedef struct CPUScheduler
{
    ProcessTable processes;
    ProcessPtr idle_process;
    mutex_t scheduler_lock;
    Tick last_tick;
    CPUID cpu;
} CPUScheduler;

typedef CPUScheduler * CPUSchedulerPtr;

OptionalDef(CPUSchedulerPtr);
ResultDef(CPUSchedulerPtr);

FixedArrayDefine(CPUSchedulerPtr, MAX_CPUS, CPUSchedulers, &Ptr__equals);

typedef struct Scheduler
{
    CPUSchedulers schedulers;
    mutex_t scheduler_lock;
    bool started;
} Scheduler;

typedef Scheduler * SchedulerPtr;

OptionalDef(SchedulerPtr);
ResultDef(SchedulerPtr);

SchedulerPtr Scheduler__heap_new();
Result(SchedulerInitStatus) Scheduler__init(SchedulerPtr self, ElfPtr idle_process);
Optional(ContextSwitch) Scheduler__interrupt(SchedulerPtr self, CPUID cpu, ProcessContextPtr context);

Optional(ProcessPtr) Scheduler__idle_process(SchedulerPtr self, CPUID cpu);

Optional(ProcessPtr) Scheduler__current_process(SchedulerPtr self, CPUID cpu);
Optional(ProcessPtr) Scheduler__save_context(SchedulerPtr self, CPUID cpu, ProcessContextPtr context);

Result(CPUID) Scheduler__schedule(SchedulerPtr self, ProcessPtr new_proc);
Result(SchedulerInitStatus) Scheduler__start(SchedulerPtr self, CPUID cpu);

#define NEW_SCHEDULER \
    (Scheduler) { \
        .schedulers = EmptyArray(CPUSchedulers),\
        .scheduler_lock = MUTEX_INIT, \
        .started = false, \
    } \

#endif