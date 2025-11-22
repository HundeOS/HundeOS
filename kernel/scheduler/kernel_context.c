#include "kernel_context.h"
#include "../kernel_core.h"

KernelContext KernelContext__new(Optional(ProcessPtr) proc, SchedulerPtr scheduler)
{
    return (KernelContext) {
        .proc = proc,
        .scheduler = scheduler
    };
}

KernelContextPtr KernelContext__heap_new(Optional(ProcessPtr) proc, SchedulerPtr scheduler)
{
    KernelContextPtr context = ptr(Kernel__alloc_kernel(sizeof(KernelContext)), KernelContext);

    *context = KernelContext__new(proc, scheduler);

    return context;
}

KernelContextPtr KernelContext__from_originating_context_heap(OriginatingContext originating_context)
{
    return KernelContext__heap_new(originating_context.process, originating_context.kernel->scheduler);
}

KernelContext KernelContext__from_originating_context(OriginatingContext originating_context)
{
    return KernelContext__new(originating_context.process, originating_context.kernel->scheduler);
}
