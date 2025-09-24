/* exception.h - Exception handling definitions */

#ifndef _EXCEPTION_H_
#define _EXCEPTION_H_

#include <stdint.h>

void vectors_init(void);

// Matches the push order in vectors_el1.S (top == current SP)
typedef struct el1_trapframe {
    uint64_t esr_el1, far_el1;      // +0
    uint64_t elr_el1, spsr_el1;     // +16
    uint64_t x30,     sp_el0;       // +32
    uint64_t x28, x29;              // +48
    uint64_t x26, x27;              // +64
    uint64_t x24, x25;              // +80
    uint64_t x22, x23;              // +96
    uint64_t x20, x21;              // +112
    uint64_t x18, x19;              // +128
    uint64_t x16, x17;              // +144
    uint64_t x14, x15;              // +160
    uint64_t x12, x13;              // +176
    uint64_t x10, x11;              // +192
    uint64_t x8,  x9;               // +208
    uint64_t x6,  x7;               // +224
    uint64_t x4,  x5;               // +240
    uint64_t x2,  x3;               // +256
    uint64_t x0,  x1;               // +272
} el1_trapframe;

// EL1 current-EL handlers
void     el1_sync_handler(el1_trapframe *tf);
void     el1_irq_handler(el1_trapframe *tf);
void     el1_fiq_handler(el1_trapframe *tf);
void     el1_serr_handler(el1_trapframe *tf);

// EL1 SVC from EL1
uint64_t el1_svc_handler(el1_trapframe *tf);

// EL0 -> EL1 handlers (AArch64 / AArch32)
void     el0_sync_handler_a64(el1_trapframe *tf);
uint64_t el0_svc_handler_a64(el1_trapframe *tf);
void     el0_irq_handler(el1_trapframe *tf);
void     el0_fiq_handler(el1_trapframe *tf);
void     el0_serr_handler(el1_trapframe *tf);
void     el0_sync_handler_a32(el1_trapframe *tf);

#endif /* _EXCEPTION_H_ */