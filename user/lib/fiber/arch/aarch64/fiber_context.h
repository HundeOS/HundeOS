#ifndef __USER_LIB_FIBER_ARCH_AARCH64_FIBER_CONTEXT_H
#define __USER_LIB_FIBER_ARCH_AARCH64_FIBER_CONTEXT_H

#include <stdint.h>

typedef struct FiberContext FiberContext;
typedef FiberContext * FiberContextPtr;

struct FiberContext
{
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t fp;
    uint64_t sp;
    uint64_t lr;
    uint64_t x0;
    uint64_t d8;
    uint64_t d9;
    uint64_t d10;
    uint64_t d11;
    uint64_t d12;
    uint64_t d13;
    uint64_t d14;
    uint64_t d15;
};

#endif
