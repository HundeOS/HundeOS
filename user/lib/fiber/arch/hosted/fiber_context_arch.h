#ifndef __USER_LIB_FIBER_ARCH_HOSTED_FIBER_CONTEXT_ARCH_H
#define __USER_LIB_FIBER_ARCH_HOSTED_FIBER_CONTEXT_ARCH_H

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <ucontext.h>

typedef struct FiberContext FiberContext;
typedef FiberContext * FiberContextPtr;

struct FiberContext
{
    ucontext_t context;
    FiberContextStart start;
    FiberPtr fiber;
    int slot;
};

#endif
