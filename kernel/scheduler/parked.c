#include "parked.h"
#include "../debug.h"
#include "../kalloc.h"

ParkedKernelContextPtr ParkedKernelContext__heap_new(KernelContextPtr kernel_context, Optional(Continuation) continuation, ParkReason park_reason)
{
    ParkedKernelContextPtr context = ptr(Kernel__alloc_kernel(sizeof(ParkedKernelContext)), ParkedKernelContext);
    *context = (ParkedKernelContext) {
        .kernel_context = kernel_context,
        .continuation = continuation,
        .park_reason = park_reason,
        .return_code = None(ReturnCode),
    };

    return context;
}

KString ParkReason__to_string(ParkReason reason)
{
    switch (reason)
    {
        case PARK_WAIT_CHILD:
            return KSTRING_NEW("PARK_WAIT_CHILD");
        case PARK_WAIT_IO:
            return KSTRING_NEW("PARK_WAIT_IO");
        case PARK_WAIT_SERVICE_EVENT:
            return KSTRING_NEW("PARK_WAIT_SERVICE_EVENT");
        default:
            return KSTRING_NEW("PARK_WAIT_UNKNOWN");
    }
}

void ParkedKernelContext__dispose(ParkedKernelContextPtr self)
{
    KernelContext__dispose_heap(self->kernel_context);
    Kernel__free(addr(self));
}
