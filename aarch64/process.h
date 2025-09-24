#ifndef __AARCH64_PROCESS_H
#define __AARCH64_PROCESS_H

#include <stdint.h>
#include "../kernel/kernel.h"

typedef struct process_context {
    // General purpose registers
    uint64_t x0,  x1,  x2,  x3,  x4,  x5,  x6,  x7;
    uint64_t x8,  x9,  x10, x11, x12, x13, x14, x15;
    uint64_t x16, x17, x18, x19, x20, x21, x22, x23;
    uint64_t x24, x25, x26, x27, x28, x29, x30;  // x30 = lr

    // Special registers
    uint64_t sp_el0;        // User stack pointer
    uint64_t elr_el1;       // Exception link register (PC to return to)
    uint64_t spsr_el1;      // Saved program status register

    // Floating point / SIMD registers (if using FP)
    __uint128_t v0,  v1,  v2,  v3,  v4,  v5,  v6,  v7;
    __uint128_t v8,  v9,  v10, v11, v12, v13, v14, v15;
    __uint128_t v16, v17, v18, v19, v20, v21, v22, v23;
    __uint128_t v24, v25, v26, v27, v28, v29, v30, v31;
    uint64_t fpsr;          // Floating-point status register
    uint64_t fpcr;          // Floating-point control register
} process_context;


typedef struct process {
    // 1. Separate memory space (page tables)
    uint64_t* page_table;     // Each process gets its own L1 table

    // 2. Saved CPU state
    process_context context; // Registers saved on context switch

    // 3. Stack
    void* stack;       // Each process has its own stack
    uint64_t stack_size;

    // 4. Process info
    uint32_t pid;
    process_state state;
} process;

#endif