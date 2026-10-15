.section .text.context, "ax"

.include "arch/aarch64/asm_macros.inc"

.global asm_switch_context
asm_switch_context:
    msr     daifset, #2

    // ------------------------------------------------------
    // 1. Enable FP/SIMD
    // ------------------------------------------------------
    mrs     x2, cpacr_el1
    orr     x2, x2, #(0b11 << 20)
    msr     cpacr_el1, x2
    isb

    // ------------------------------------------------------
    // 2. Load SP to the ProcessContext in kernel space
    // ------------------------------------------------------

    msr     ttbr0_el1, x1

    tlbi vmalle1
    dsb ish
    isb
    reset_kernel_stack
    restore_parked_context


    // --- Memory barrier for clean return ---
    dsb     sy
    isb
    // A different process may have consumed this core's event before resumption.
    sevl
    eret

.global asm_switch_kernel_context
asm_switch_kernel_context:
    //mov     sp, x0

    // ------------------------------------------------------
    // 1. Enable FP/SIMD
    // ------------------------------------------------------
    mrs     x2, cpacr_el1
    orr     x2, x2, #(0b11 << 20)
    msr     cpacr_el1, x2
    isb

    // ------------------------------------------------------
    // 2. Load SP to the ProcessContext in kernel space
    // ------------------------------------------------------

    msr     ttbr0_el1, x1
    tlbi vmalle1
    dsb ish
    isb
    ret
