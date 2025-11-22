#include "originating_call_context.h"

OriginatingContext OriginatingContext__syscall(KernelPtr kernel, CallContext syscall)
{
    return (OriginatingContext) {
        .type = CALL_TYPE_SYSCALL,
        .kernel = kernel,
        .process = syscall.process_opt,

        .call = {
            .syscall = syscall,
        },
    };
}

OriginatingContext OriginatingContext__irq(KernelPtr kernel, Optional(ProcessPtr) process)
{
    return (OriginatingContext) {
        .process = process,
        .type = CALL_TYPE_IRQ,
        .kernel = kernel,
        .call = {
            .irq = (IRQCallContext) {}
        }
    };
}

OriginatingContext OriginatingContext__fault(KernelPtr kernel, Optional(ProcessPtr) process)
{
    return (OriginatingContext) {
        .kernel = kernel,
        .type = CALL_TYPE_FAULT,
        .process = process,
        .call = {
            .fault = (FaultCallContext) {}
        },
    };
}
