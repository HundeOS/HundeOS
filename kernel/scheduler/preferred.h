#ifndef KERNEL_PREFERRED_H
#define KERNEL_PREFERRED_H

#include "scheduler.h"

#define SCHEDULER_WARM_RESIDENCY_USEC 10000ULL
#define SCHEDULER_PREFERRED_BUDGET_USEC 10000ULL

// All operations are called with the scheduler lock held, without switching context.
void Scheduler__refresh_preferred(SchedulerPtr self);
Optional(ProcessPtr) Scheduler__take_preferred(SchedulerPtr self, CPUSchedulerPtr cpu);
void Scheduler__forget_preferred(SchedulerPtr self, ProcessPtr process);
bool Scheduler__ready_exec_continuation_waiting(SchedulerPtr self, ProcessPtr current);
bool Process__retain_preferred(ProcessPtr self);

#endif
