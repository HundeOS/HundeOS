#ifndef __KERNEL_SCHEDULER_KERNEL_CONTEXT_H
#define __KERNEL_SCHEDULER_KERNEL_CONTEXT_H

#include "../process.h"
#include "../context/originating_call_context.h"
#include "../scheduler/types.h"

typedef struct KernelContext
{
    Optional(ProcessID) pid;
    Optional(SchedulerPtr) scheduler;
} KernelContext;

typedef KernelContext * KernelContextPtr;

KernelContext KernelContext__new(
    Optional(ProcessID) pid, Optional(SchedulerPtr) scheduler
);

KernelContextPtr KernelContext__heap_new(
    Optional(ProcessID) pid, Optional(SchedulerPtr) scheduler
);

KernelContext KernelContext__from_originating_context(OriginatingContext originating_context);
KernelContextPtr KernelContext__from_originating_context_heap(OriginatingContext originating_context);

#define NEW_KERNEL_CONTEXT(sched, proc_id) \
    (KernelContext) { \
        .pid = proc_id, \
        .scheduler = Some(SchedulerPtr, sched),\
    } \


#define EMPTY_KERNEL_CONTEXT \
    (KernelContext) { \
        .pid = None(ProcessID),\
        .scheduler = None(SchedulerPtr),\
    }


#endif
