#ifndef __RISCV64_PROCESS_H
#define __RISCV64_PROCESS_H

#include <stdint.h>

typedef struct __attribute__((aligned(16))) ProcessContext
{
    uint64_t x[32];
    uint64_t sepc;
    uint64_t sstatus;
    uint64_t scause;
    uint64_t stval;
} ProcessContext;

#endif
