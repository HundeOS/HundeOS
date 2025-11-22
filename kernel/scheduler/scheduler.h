#ifndef __KERNEL_SCHEDULER_SCHEDULER_H
#define __KERNEL_SCHEDULER_SCHEDULER_H

#include <stdint.h>

#include "../types/result.h"
#include "../types/fixed_array.h"
#include "kernel_context.h"
#include "parked.h"
#include "context_switch.h"
#include "../process.h"

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
    SCHEDULER_NOT_STARTED = -3,
    SCHEDULER_PROCESS_NOT_FOUND = -4,
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

Result(SchedulerInitStatus) Scheduler__init(SchedulerPtr self, Elf idle_process);
void Scheduler__preempt(SchedulerPtr self, CPUID cpu, ProcessContextPtr context);
Result(ProcessPtr) Scheduler__exit(SchedulerPtr scheduler, ProcessPtr proc);

Result(ProcessPtr) Scheduler__park(
    SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context, Optional(Continuation) kernel_continuation
);
Result(ProcessPtr) Scheduler__unpark(SchedulerPtr self, ProcessPtr proc);

Optional(ProcessPtr) Scheduler__idle_process(SchedulerPtr self, CPUID cpu);
Optional(ProcessPtr) Scheduler__current_process(SchedulerPtr self, CPUID cpu);
ProcessTablePtr Scheduler__all_processes(SchedulerPtr self, CPUID cpu);

uint64_t Scheduler__process_count(SchedulerPtr self);

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