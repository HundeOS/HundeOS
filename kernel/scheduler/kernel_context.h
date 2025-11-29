#ifndef __KERNEL_SCHEDULER_KERNEL_CONTEXT_H
#define __KERNEL_SCHEDULER_KERNEL_CONTEXT_H

#include "../process.h"
#include "../context/originating_call_context.h"
#include "../scheduler/types.h"

typedef struct KernelContext
{
    Optional(ProcessPtr) proc;
    Optional(ProcessID) pid;
    Optional(SchedulerPtr) scheduler;
} KernelContext;

typedef KernelContext * KernelContextPtr;

KernelContext KernelContext__new(
    Optional(ProcessPtr) proc, Optional(ProcessID) pid, Optional(SchedulerPtr) scheduler
);

KernelContextPtr KernelContext__heap_new(
    Optional(ProcessPtr) proc, Optional(ProcessID) pid, Optional(SchedulerPtr) scheduler
);

KernelContext KernelContext__from_originating_context(OriginatingContext originating_context);
KernelContextPtr KernelContext__from_originating_context_heap(OriginatingContext originating_context);

#define NEW_KERNEL_CONTEXT(sched, proc_id, process) \
    (KernelContext) { \
        .proc = process,\
        .pid = proc_id, \
        .scheduler = sched,\
    } \


#define EMPTY_KERNEL_CONTEXT \
    (KernelContext) { \
        .proc = None(ProcessPtr),\
        .pid = None(ProcessID),\
        .scheduler = NULL,\
    }


#endif