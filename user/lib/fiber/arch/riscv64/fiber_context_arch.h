#ifndef __USER_LIB_FIBER_ARCH_RISCV64_FIBER_CONTEXT_ARCH_H
#define __USER_LIB_FIBER_ARCH_RISCV64_FIBER_CONTEXT_ARCH_H

#include <stdint.h>

typedef struct FiberContext FiberContext;
typedef FiberContext * FiberContextPtr;

struct FiberContext
{
    uint64_t ra;
    uint64_t sp;
    uint64_t s0;
    uint64_t s1;
    uint64_t s2;
    uint64_t s3;
    uint64_t s4;
    uint64_t s5;
    uint64_t s6;
    uint64_t s7;
    uint64_t s8;
    uint64_t s9;
    uint64_t s10;
    uint64_t s11;
    uint64_t a0;
};

#endif
