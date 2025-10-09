// boot.S - Multi-core aware bootloader for RPi3

    .section .boot.text, "ax"
    .align  7
    .global _start
_start:
    // Get CPU ID first
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF        // Extract CPU ID (0-3)
    cbz     x0, bsp_boot         // CPU0 = BSP, continue normal boot

    // Secondary CPUs go to holding pen
    hold:
    wfe
    b       hold
//============= BSP (CPU0) Boot Path =============
bsp_boot:
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

    // Prepare EL1 stack
    ldr     x1, =el1_stack_top
    msr     sp_el1, x1
    ldr     x1, =boot_vector_table
    msr     vbar_el1, x1
    isb

    // Minimal SCTLR_EL1 baseline (MMU/caches off)
    mrs     x1, sctlr_el1
    bic     x1, x1, #(1 << 0)       // M=0
    bic     x1, x1, #(1 << 2)       // C=0
    bic     x1, x1, #(1 << 12)      // I=0
    msr     sctlr_el1, x1
    isb

    // Drop to EL1h at el1_start
    ldr     x1, =el1_start
    msr     elr_el2, x1
    mov     x1, #0b0101             // SPSR_EL2 M[3:0]=EL1h
    orr     x1, x1, #(0b1111 << 6)  // keep masked
    msr     spsr_el2, x1
    eret

//---------------- if we already started at EL1 ----------------
el1_entry_direct:
    ldr     x1, =el1_stack_top
    mov     sp, x1

//---------------- common EL1 entry ----------------
el1_start:
    // Clear .bss (only BSP should do this)
    ldr     x0, =__boot_bss_start
    ldr     x1, =__boot_bss_end
    mov     x2, #0
1:  cmp     x0, x1
    b.hs    2f
    str     x2, [x0], #8
    b       1b
2:
    // Call C main
    bl primary_booted

hang:
    wfe
    b       hang

//---------------- stacks ----------------
    .section .boot.bss.stack, "aw", %nobits
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


.section .boot.vectors, "ax"
.align 11  // Vector table must be 2048-byte aligned

.global boot_vector_table
boot_vector_table:
    // Current EL with SP0
    .align 7
boot_curr_el_sp0_sync:
    b boot_sync_handler_current_el_sp0
    .align 7
boot_curr_el_sp0_irq:
    b boot_irq_handler_current_el_sp0
    .align 7
boot_curr_el_sp0_fiq:
    b boot_fiq_handler_current_el_sp0
    .align 7
boot_curr_el_sp0_serror:
    b boot_serror_handler_current_el_sp0

    // Current EL with SPx
    .align 7
boot_curr_el_spx_sync:
    b boot_sync_handler_current_el_spx
    .align 7
boot_curr_el_spx_irq:
    b boot_irq_handler_current_el_spx
    .align 7
boot_curr_el_spx_fiq:
    b boot_fiq_handler_current_el_spx
    .align 7
boot_curr_el_spx_serror:
    b boot_serror_handler_current_el_spx

    // Lower EL using AArch64
    .align 7
boot_lower_el_aarch64_sync:
    b boot_sync_handler_boot_lower_el_aarch64
    .align 7
boot_lower_el_aarch64_irq:
    b boot_irq_handler_boot_lower_el_aarch64
    .align 7
boot_lower_el_aarch64_fiq:
    b boot_fiq_handler_boot_lower_el_aarch64
    .align 7
boot_lower_el_aarch64_serror:
    b boot_serror_handler_boot_lower_el_aarch64

    // Lower EL using AArch32
    .align 7
boot_lower_el_aarch32_sync:
    b boot_sync_handler_boot_lower_el_aarch32
    .align 7
boot_lower_el_aarch32_irq:
    b boot_irq_handler_boot_lower_el_aarch32
    .align 7
boot_lower_el_aarch32_fiq:
    b boot_fiq_handler_boot_lower_el_aarch32
    .align 7
boot_lower_el_aarch32_serror:
    b boot_serror_handler_boot_lower_el_aarch32

.macro boot_save_context
    sub sp, sp, #256
    stp x0, x1, [sp, #16 * 0]
    stp x2, x3, [sp, #16 * 1]
    stp x4, x5, [sp, #16 * 2]
    stp x6, x7, [sp, #16 * 3]
    stp x8, x9, [sp, #16 * 4]
    stp x10, x11, [sp, #16 * 5]
    stp x12, x13, [sp, #16 * 6]
    stp x14, x15, [sp, #16 * 7]
    stp x16, x17, [sp, #16 * 8]
    stp x18, x19, [sp, #16 * 9]
    stp x20, x21, [sp, #16 * 10]
    stp x22, x23, [sp, #16 * 11]
    stp x24, x25, [sp, #16 * 12]
    stp x26, x27, [sp, #16 * 13]
    stp x28, x29, [sp, #16 * 14]
    str x30, [sp, #16 * 15]
.endm

.macro boot_restore_context
    ldr x30, [sp, #16 * 15]
    ldp x28, x29, [sp, #16 * 14]
    ldp x26, x27, [sp, #16 * 13]
    ldp x24, x25, [sp, #16 * 12]
    ldp x22, x23, [sp, #16 * 11]
    ldp x20, x21, [sp, #16 * 10]
    ldp x18, x19, [sp, #16 * 9]
    ldp x16, x17, [sp, #16 * 8]
    ldp x14, x15, [sp, #16 * 7]
    ldp x12, x13, [sp, #16 * 6]
    ldp x10, x11, [sp, #16 * 5]
    ldp x8, x9, [sp, #16 * 4]
    ldp x6, x7, [sp, #16 * 3]
    ldp x4, x5, [sp, #16 * 2]
    ldp x2, x3, [sp, #16 * 1]
    ldp x0, x1, [sp, #16 * 0]
    add sp, sp, #256
.endm

.macro boot_restore_context_preserve_x0
    ldr x30, [sp, #16 * 15]
    ldp x28, x29, [sp, #16 * 14]
    ldp x26, x27, [sp, #16 * 13]
    ldp x24, x25, [sp, #16 * 12]
    ldp x22, x23, [sp, #16 * 11]
    ldp x20, x21, [sp, #16 * 10]
    ldp x18, x19, [sp, #16 * 9]
    ldp x16, x17, [sp, #16 * 8]
    ldp x14, x15, [sp, #16 * 7]
    ldp x12, x13, [sp, #16 * 6]
    ldp x10, x11, [sp, #16 * 5]
    ldp x8, x9, [sp, #16 * 4]
    ldp x6, x7, [sp, #16 * 3]
    ldp x4, x5, [sp, #16 * 2]
    ldp x2, x3, [sp, #16 * 1]
    ldr     x1, [sp, #8]           // restore x1 only
    add sp, sp, #256
.endm




.global handler_boot
boot_sync_handler_current_el_sp0:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context_preserve_x0
    eret

.global boot_irq_handler_current_el_sp0
boot_irq_handler_current_el_sp0:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret

.global boot_fiq_handler_current_el_sp0
boot_fiq_handler_current_el_sp0:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret

.global boot_serror_handler_current_el_sp0
boot_serror_handler_current_el_sp0:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret

// Current EL with SPx handlers
.global boot_sync_handler_current_el_spx
boot_sync_handler_current_el_spx:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context_preserve_x0
    eret

.global boot_irq_handler_current_el_spx
boot_irq_handler_current_el_spx:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret

.global boot_fiq_handler_current_el_spx
boot_fiq_handler_current_el_spx:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret

.global boot_serror_handler_current_el_spx
boot_serror_handler_current_el_spx:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret

// Lower EL AArch64 handlers
.global boot_sync_handler_boot_lower_el_aarch64
boot_sync_handler_boot_lower_el_aarch64:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context_preserve_x0
    eret

.global boot_irq_handler_boot_lower_el_aarch64
boot_irq_handler_boot_lower_el_aarch64:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret

.global boot_fiq_handler_boot_lower_el_aarch64
boot_fiq_handler_boot_lower_el_aarch64:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret

.global boot_serror_handler_boot_lower_el_aarch64
boot_serror_handler_boot_lower_el_aarch64:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret

// Lower EL AArch32 handlers
.global boot_sync_handler_boot_lower_el_aarch32
boot_sync_handler_boot_lower_el_aarch32:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context_preserve_x0
    eret

.global boot_irq_handler_boot_lower_el_aarch32
boot_irq_handler_boot_lower_el_aarch32:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret

.global boot_fiq_handler_boot_lower_el_aarch32
boot_fiq_handler_boot_lower_el_aarch32:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret

.global boot_serror_handler_boot_lower_el_aarch32
boot_serror_handler_boot_lower_el_aarch32:
    boot_save_context
    mov x0, sp
    bl boot_handler
    boot_restore_context
    eret