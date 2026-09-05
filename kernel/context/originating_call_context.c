#include "originating_call_context.h"

static OriginatingContext OriginatingContext__new(
    KernelPtr kernel, CallType type, OptionalPtr(ProcessPtr) process
)
{
    return (OriginatingContext) {
        .type = type,
        .kernel = kernel,
        .process = *process,
    };
}

static IRQCallContext IRQCallContext__new()
{
    return (IRQCallContext) {};
}

static FaultCallContext FaultCallContext__new()
{
    return (FaultCallContext) {};
}

OriginatingContext OriginatingContext__syscall(KernelPtr kernel, const CallContext * syscall)
{
    OriginatingContext context = OriginatingContext__new(kernel, CALL_TYPE_SYSCALL, &syscall->process_opt);
    context.call.syscall = *syscall;
    return context;
}

OriginatingContext OriginatingContext__irq(KernelPtr kernel, OptionalPtr(ProcessPtr) process)
{
    OriginatingContext context = OriginatingContext__new(kernel, CALL_TYPE_IRQ, process);
    context.call.irq = IRQCallContext__new();
    return context;
}

OriginatingContext OriginatingContext__fault(KernelPtr kernel, OptionalPtr(ProcessPtr) process)
{
    OriginatingContext context = OriginatingContext__new(kernel, CALL_TYPE_FAULT, process);
    context.call.fault = FaultCallContext__new();
    return context;
}
