#ifndef __KERNEL_CONTEXT_ORIGINATING_CALL_CONTEXT_H
#define __KERNEL_CONTEXT_ORIGINATING_CALL_CONTEXT_H

#include "../syscall/syscall.h"

typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

typedef uint64_t CallType;

typedef enum CallTypes
{
    CALL_TYPE_SYSCALL = 0,
    CALL_TYPE_IRQ = 1,
    CALL_TYPE_FAULT = 2
}
CallTypes;

typedef struct IRQCallContext
{
}
IRQCallContext;

typedef struct FaultCallContext
{
}
FaultCallContext;

typedef struct OriginatingContext
{
    CallType type;
    KernelPtr kernel;
    Optional(ProcessPtr) process;

    union
    {
        CallContext syscall;
        IRQCallContext irq;
        FaultCallContext fault;
    } call;
}
OriginatingContext;
typedef OriginatingContext * OriginatingContextPtr;
typedef const OriginatingContext * ConstOriginatingContextPtr;

OriginatingContext OriginatingContext__syscall(KernelPtr kernel, const CallContext * syscall);
OriginatingContext OriginatingContext__irq(KernelPtr kernel, OptionalPtr(ProcessPtr) process);
OriginatingContext OriginatingContext__fault(KernelPtr kernel, OptionalPtr(ProcessPtr) process);

#endif
