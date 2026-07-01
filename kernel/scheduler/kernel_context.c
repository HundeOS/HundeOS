#include "kernel_context.h"
#include "../kernel_core.h"
#include "../kalloc.h"

KernelContext KernelContext__new(Optional(ProcessID) pid, Optional(SchedulerPtr) scheduler)
{
    return (KernelContext) {
        .pid = pid,
        .scheduler = scheduler,
        .result_mapper = None(KernelResultMapper),
        .result_mapper_context = 0,
    };
}

KernelContextPtr KernelContext__heap_new(Optional(ProcessID) pid, Optional(SchedulerPtr) scheduler)
{
    KernelContextPtr context = ptr(Kernel__alloc_kernel(sizeof(KernelContext)), KernelContext);

    *context = KernelContext__new(pid, scheduler);

    return context;
}

KernelContextPtr KernelContext__from_originating_context_heap(OriginatingContext originating_context)
{
    Optional(ProcessID) pid = None(ProcessID);
    Match(ProcessPtr, originating_context.process)
        IsSome(process, pid = Some(ProcessID, process->pid));
    End

    return KernelContext__heap_new(pid, Some(SchedulerPtr, originating_context.kernel->scheduler));
}

KernelContext KernelContext__from_originating_context(OriginatingContext originating_context)
{
    Optional(ProcessID) pid = None(ProcessID);
    Match(ProcessPtr, originating_context.process)
        IsSome(process, pid = Some(ProcessID, process->pid));
    End

    return KernelContext__new(pid, Some(SchedulerPtr, originating_context.kernel->scheduler));
}
