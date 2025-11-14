#include "parked.h"
#include "../debug.h"

ParkedKernelContextPtr ParkedKernelContext__heap_new(KernelContextPtr kernel_context, Optional(Continuation) continuation)
{
    ParkedKernelContextPtr context = ptr(Kernel__alloc_kernel(sizeof(ParkedKernelContext)), ParkedKernelContext);
    *context = (ParkedKernelContext) {
        .kernel_context = kernel_context,
        .continuation = continuation
    };

    return context;
}

void ParkedKernelContext__dispose(ParkedKernelContextPtr self)
{
    __d_println(PANIC, "ParkedKernelContext__dispose needs to free KernelContext");
}
