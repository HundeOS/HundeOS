#ifndef __KERNEL_SCHEDULER_KERNEL_CONTEXT_H
#define __KERNEL_SCHEDULER_KERNEL_CONTEXT_H

#include "../process.h"

typedef struct Scheduler Scheduler;
typedef Scheduler * SchedulerPtr;

typedef struct KernelContext
{
    ProcessPtr proc;
    SchedulerPtr scheduler;
} KernelContext;

typedef KernelContext * KernelContextPtr;

#endif