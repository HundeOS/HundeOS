#ifndef __KERNNEL_SCHEDULER_PARKED_H
#define __KERNNEL_SCHEDULER_PARKED_H

#include <stdint.h>

#include "../types/result.h"
#include "../types/optional.h"
#include "../types/kstring.h"
#include "../process.h"
#include "kernel_context.h"
#include "types.h"

typedef uint64_t ParkReason;

typedef enum ParkReasons
{
    PARK_WAIT_CHILD = 1,
    PARK_WAIT_IO = 2,
    PARK_WAIT_SERVICE_EVENT = 3,
} ParkReasons;

typedef struct ParkedKernelContext
{
    KernelContextPtr kernel_context;
    Optional(Continuation) continuation;
    ParkReason park_reason;
    Optional(ReturnCode) return_code;
} ParkedKernelContext;

typedef ParkedKernelContext * ParkedKernelContextPtr;
OptionalDef(ParkedKernelContext);


ParkedKernelContextPtr ParkedKernelContext__heap_new(
    KernelContextPtr kernel_context, Optional(Continuation) continuation, ParkReason park_reason
);

KString ParkReason__to_string(ParkReason reason);

void ParkedKernelContext__dispose(ParkedKernelContextPtr self);

#endif
