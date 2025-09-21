// boot.S — drop to EL1; keep IRQ/FIQ masked; call C main(); vectors set in C.

    .section .text.boot, "ax"
    .align  7
    .global _start
_start:
    // Mask during early bring-up
    msr     daifset, #0b1111
    isb

    // Detect current EL
    mrs     x0, CurrentEL
    lsr     x0, x0, #2          // 3=EL3, 2=EL2, 1=EL1
    cmp     x0, #3
    b.eq    el3_enter
    cmp     x0, #2
    b.eq    el2_enter
    // else already EL1
    b       el1_entry_direct

//---------------- EL3 -> EL2 ----------------
el3_enter:
    // EL3 stack
    ldr     x1, =el3_stack_top
    mov     sp, x1

    // SCR_EL3: NS=1 (non-secure), RW=1 (64-bit), HCE=1 (enable EL2)
    mrs     x1, scr_el3
    orr     x1, x1, #(1 << 0)       // NS
    orr     x1, x1, #(1 << 10)      // RW
    orr     x1, x1, #(1 << 8)       // HCE
    msr     scr_el3, x1
    isb

    // ERET to EL2h
    ldr     x1, =el2_entry_from_el3
    msr     elr_el3, x1
    mov     x1, #0b1001             // SPSR_EL3 M[3:0]=EL2h
    orr     x1, x1, #(0b1111 << 6)  // keep masked on entry
    msr     spsr_el3, x1
    eret

//---------------- enter / run at EL2 ----------------
el2_enter:
    ldr     x1, =el2_stack_top
    mov     sp, x1
el2_entry_from_el3:
    // Make EL1 AArch64
    mrs     x1, hcr_el2
    orr     x1, x1, #(1 << 31)      // HCR_EL2.RW=1
    msr     hcr_el2, x1
    isb

    // Prepare EL1 stack so we can ERET into it
    ldr     x1, =el1_stack_top
    msr     sp_el1, x1

    // Minimal SCTLR_EL1 baseline (MMU/caches off)
    mrs     x1, sctlr_el1
    bic     x1, x1, #(1 << 0)       // M=0
    bic     x1, x1, #(1 << 2)       // C=0
    bic     x1, x1, #(1 << 12)      // I=0
    msr     sctlr_el1, x1
    isb

    // (Optional) You may preinstall EL1 vectors here from EL2:
    //   ldr x1, =vbar_el1_vector
    //   msr vbar_el1, x1
    //   isb

    // Drop to EL1h at el1_start (DAIF masked on entry)
    ldr     x1, =el1_start
    msr     elr_el2, x1
    mov     x1, #0b0101             // SPSR_EL2 M[3:0]=EL1h
    orr     x1, x1, #(0b1111 << 6)  // keep masked until EL1 code unmasks
    msr     spsr_el2, x1
    eret

//---------------- if we already started at EL1 ----------------
el1_entry_direct:
    ldr     x1, =el1_stack_top
    mov     sp, x1

//---------------- common EL1 entry ----------------
el1_start:
    // Clear .bss
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
    mov     x2, #0
1:  cmp     x0, x1
    b.hs    2f
    str     x2, [x0], #8
    b       1b
2:
    // *** Do NOT unmask here. ***
    // Install VBAR_EL1 in C (main) first, then unmask IRQs in C.

    // Call C main() with DAIF still masked
    bl      main

hang:
    wfe
    b       hang

//---------------- stacks ----------------
    .section .bss.stack, "aw", %nobits
    .balign 16
    .global el3_stack_top, el2_stack_top, el1_stack_top

    .set EL3_STACK_SIZE, (8 * 1024)
    .set EL2_STACK_SIZE, (16 * 1024)
    .set EL1_STACK_SIZE, (32 * 1024)

el3_stack:      .space EL3_STACK_SIZE
el3_stack_top:
el2_stack:      .space EL2_STACK_SIZE
el2_stack_top:
el1_stack:      .space EL1_STACK_SIZE
el1_stack_top: