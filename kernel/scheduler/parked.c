#include "parked.h"
#include "../debug.h"

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

void ParkedKernelContext__dispose(ParkedKernelContextPtr self)
{
    __d_println(PANIC, "ParkedKernelContext__dispose needs to free KernelContext");
}
