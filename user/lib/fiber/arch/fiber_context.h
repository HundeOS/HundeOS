#ifndef __USER_LIB_FIBER_ARCH_FIBER_CONTEXT_H
#define __USER_LIB_FIBER_ARCH_FIBER_CONTEXT_H

#include <stddef.h>

#include "../../../../common/types/address.h"
#include "../../../../common/types/result.h"

typedef struct Fiber Fiber;
typedef Fiber * FiberPtr;

typedef void (*FiberContextStart)(FiberPtr fiber);

#if __STDC_HOSTED__
#include "hosted/fiber_context.h"
#else
#include "aarch64/fiber_context.h"
#endif

Result(FiberCode) FiberContext__init(FiberContextPtr self, Address stack_base, size_t stack_size,
                                     FiberContextStart start, FiberPtr fiber);
void FiberContext__switch(FiberContextPtr self, FiberContextPtr next);

#endif
