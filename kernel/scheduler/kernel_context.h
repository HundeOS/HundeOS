#ifndef __KERNEL_SCHEDULER_KERNEL_CONTEXT_H
#define __KERNEL_SCHEDULER_KERNEL_CONTEXT_H

#include "../process.h"
#include "../context/originating_call_context.h"
#include "../scheduler/types.h"

typedef Result(Anything) (*KernelResultMapper)(ResultPtr(Anything) result, Anything context);
OptionalDef(KernelResultMapper);

typedef struct KernelContext KernelContext;
typedef KernelContext * KernelContextPtr;
typedef const KernelContext * ConstKernelContextPtr;
OptionalDef(KernelContextPtr);

struct KernelContext
{
    Optional(ProcessID) pid;
    Optional(SchedulerPtr) scheduler;
    Optional(KernelResultMapper) result_mapper;
    Optional(KernelContextPtr) next_context;
    Anything result_mapper_context;
};

KernelContext KernelContext__new(OptionalPtr(ProcessID) pid, OptionalPtr(SchedulerPtr) scheduler);

KernelContextPtr KernelContext__heap_new(OptionalPtr(ProcessID) pid, OptionalPtr(SchedulerPtr) scheduler);
KernelContextPtr KernelContext__heap_new_with_mapper(OptionalPtr(ProcessID) pid, OptionalPtr(SchedulerPtr) scheduler,
                                                     KernelResultMapper mapper, Anything mapper_context);
void KernelContext__set_result_mapper(KernelContextPtr self, KernelResultMapper mapper, Anything mapper_context);
Result(Anything) KernelContext__map_result(KernelContextPtr self, ResultPtr(Anything) result);
bool KernelContext__has_any_state(KernelContextPtr self);
void KernelContext__dispose_heap(KernelContextPtr self);

KernelContext KernelContext__from_originating_context(ConstOriginatingContextPtr originating_context);
KernelContextPtr KernelContext__from_originating_context_heap(ConstOriginatingContextPtr originating_context);

#define NEW_KERNEL_CONTEXT(sched, proc_id)                                                                             \
    (KernelContext)                                                                                                    \
    {                                                                                                                  \
        .pid = proc_id, .scheduler = Some(SchedulerPtr, sched), .result_mapper = None(KernelResultMapper),             \
        .result_mapper_context = 0,                                                                                    \
    }

#define EMPTY_KERNEL_CONTEXT                                                                                           \
    (KernelContext)                                                                                                    \
    {                                                                                                                  \
        .pid = None(ProcessID), .scheduler = None(SchedulerPtr), .result_mapper = None(KernelResultMapper),            \
        .result_mapper_context = 0,                                                                                    \
    }

#endif
