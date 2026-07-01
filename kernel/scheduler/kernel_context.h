#ifndef __KERNEL_SCHEDULER_KERNEL_CONTEXT_H
#define __KERNEL_SCHEDULER_KERNEL_CONTEXT_H

#include "../process.h"
#include "../context/originating_call_context.h"
#include "../scheduler/types.h"

typedef Result(Anything) (* KernelResultMapper)(Result(Anything) result, Anything context);
OptionalDef(KernelResultMapper);

typedef struct KernelContext
{
    Optional(ProcessID) pid;
    Optional(SchedulerPtr) scheduler;
    Optional(KernelResultMapper) result_mapper;
    Anything result_mapper_context;
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
        .result_mapper = None(KernelResultMapper),\
        .result_mapper_context = 0,\
    } \


#define EMPTY_KERNEL_CONTEXT \
    (KernelContext) { \
        .pid = None(ProcessID),\
        .scheduler = None(SchedulerPtr),\
        .result_mapper = None(KernelResultMapper),\
        .result_mapper_context = 0,\
    }


#endif
