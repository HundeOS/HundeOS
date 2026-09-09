#ifndef __AARCH64_PROCESS_H
#define __AARCH64_PROCESS_H

#include <stdint.h>
#include "../../kernel/kernel.h"

typedef struct __attribute__((packed, aligned(16))) ProcessContext
{
    uint64_t x[31]; // x0–x30
    uint64_t sp;    // sp_el0 (or saved SP)
    uint64_t elr;   // elr_el1
    uint64_t spsr;  // spsr_el1

    __uint128_t q[32]; // q0–q31, 128-bit SIMD/FPU regs

    uint64_t fpcr; // Floating-point control register
    uint64_t fpsr; // Floating-point status register
} ProcessContext;

#endif