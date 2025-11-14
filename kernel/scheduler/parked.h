#ifndef __KERNNEL_SCHEDULER_PARKED_H
#define __KERNNEL_SCHEDULER_PARKED_H

#include "../types/types.h"
#include "../types/result.h"
#include "../process.h"
#include "kernel_context.h"

typedef Result(Anything) (* Continuation)(void * context);

OptionalDef(Continuation);

typedef struct ParkedKernelContext
{
    KernelContextPtr kernel_context;
    Optional(Continuation) continuation;
} ParkedKernelContext;

typedef ParkedKernelContext * ParkedKernelContextPtr;
OptionalDef(ParkedKernelContext);
// OptionalDef(ParkedKernelContextPtr);

ParkedKernelContextPtr ParkedKernelContext__heap_new(
        KernelContextPtr kernel_context, Optional(Continuation) continuation
    );

void ParkedKernelContext__dispose(ParkedKernelContextPtr self);

#endif