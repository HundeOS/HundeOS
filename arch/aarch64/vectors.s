.section .text.vectors, "ax"
.align 11  // Vector table must be 2048-byte aligned

.global vector_table
vector_table:
    // Current EL with SP0
    .align 7
curr_el_sp0_sync:
    b sync_handler_current_el_sp0
    .align 7
curr_el_sp0_irq:
    b irq_handler_current_el_sp0
    .align 7
curr_el_sp0_fiq:
    b fiq_handler_current_el_sp0
    .align 7
curr_el_sp0_serror:
    b serror_handler_current_el_sp0

    // Current EL with SPx
    .align 7
curr_el_spx_sync:
    b sync_handler_current_el_spx
    .align 7
curr_el_spx_irq:
    b irq_handler_current_el_spx
    .align 7
curr_el_spx_fiq:
    b fiq_handler_current_el_spx
    .align 7
curr_el_spx_serror:
    b serror_handler_current_el_spx

    // Lower EL using AArch64
    .align 7
lower_el_aarch64_sync:
    b sync_handler_lower_el_aarch64
    .align 7
lower_el_aarch64_irq:
    b irq_handler_lower_el_aarch64
    .align 7
lower_el_aarch64_fiq:
    b fiq_handler_lower_el_aarch64
    .align 7
lower_el_aarch64_serror:
    b serror_handler_lower_el_aarch64

    // Lower EL using AArch32
    .align 7
lower_el_aarch32_sync:
    b sync_handler_lower_el_aarch32
    .align 7
lower_el_aarch32_irq:
    b irq_handler_lower_el_aarch32
    .align 7
lower_el_aarch32_fiq:
    b fiq_handler_lower_el_aarch32
    .align 7
lower_el_aarch32_serror:
    b serror_handler_lower_el_aarch32

.macro save_context
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

.macro restore_context
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

.macro restore_context_preserve_x0
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



// Current EL with SP0 handlers
.global sync_handler_current_el_sp0
sync_handler_current_el_sp0:
    save_context
    mov x0, sp
    bl c_sync_handler_current_el_sp0
    restore_context_preserve_x0
    eret

.global irq_handler_current_el_sp0
irq_handler_current_el_sp0:
    save_context
    mov x0, sp
    bl c_irq_handler_current_el_sp0
    restore_context
    eret

.global fiq_handler_current_el_sp0
fiq_handler_current_el_sp0:
    save_context
    mov x0, sp
    bl c_fiq_handler_current_el_sp0
    restore_context
    eret

.global serror_handler_current_el_sp0
serror_handler_current_el_sp0:
    save_context
    mov x0, sp
    bl c_serror_handler_current_el_sp0
    restore_context
    eret

// Current EL with SPx handlers
.global sync_handler_current_el_spx
sync_handler_current_el_spx:
    save_context
    mov x0, sp
    bl c_sync_handler_current_el_spx
    restore_context_preserve_x0
    eret

.global irq_handler_current_el_spx
irq_handler_current_el_spx:
    save_context
    mov x0, sp
    bl c_irq_handler_current_el_spx
    restore_context
    eret

.global fiq_handler_current_el_spx
fiq_handler_current_el_spx:
    save_context
    mov x0, sp
    bl c_fiq_handler_current_el_spx
    restore_context
    eret

.global serror_handler_current_el_spx
serror_handler_current_el_spx:
    save_context
    mov x0, sp
    bl c_serror_handler_current_el_spx
    restore_context
    eret

// Lower EL AArch64 handlers
.global sync_handler_lower_el_aarch64
sync_handler_lower_el_aarch64:
    save_context
    mov x0, sp
    bl c_sync_handler_lower_el_aarch64
    restore_context_preserve_x0
    eret

.global irq_handler_lower_el_aarch64
irq_handler_lower_el_aarch64:
    save_context
    mov x0, sp
    bl c_irq_handler_lower_el_aarch64
    restore_context
    eret

.global fiq_handler_lower_el_aarch64
fiq_handler_lower_el_aarch64:
    save_context
    mov x0, sp
    bl c_fiq_handler_lower_el_aarch64
    restore_context
    eret

.global serror_handler_lower_el_aarch64
serror_handler_lower_el_aarch64:
    save_context
    mov x0, sp
    bl c_serror_handler_lower_el_aarch64
    restore_context
    eret

// Lower EL AArch32 handlers
.global sync_handler_lower_el_aarch32
sync_handler_lower_el_aarch32:
    save_context
    mov x0, sp
    bl c_sync_handler_lower_el_aarch32
    restore_context_preserve_x0
    eret

.global irq_handler_lower_el_aarch32
irq_handler_lower_el_aarch32:
    save_context
    mov x0, sp
    bl c_irq_handler_lower_el_aarch32
    restore_context
    eret

.global fiq_handler_lower_el_aarch32
fiq_handler_lower_el_aarch32:
    save_context
    mov x0, sp
    bl c_fiq_handler_lower_el_aarch32
    restore_context
    eret

.global serror_handler_lower_el_aarch32
serror_handler_lower_el_aarch32:
    save_context
    mov x0, sp
    bl c_serror_handler_lower_el_aarch32
    restore_context
    eret