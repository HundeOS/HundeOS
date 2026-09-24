.section .text.vectors, "ax"
.align 11  // Vector table must be 2048-byte aligned

.include "arch/aarch64/asm_macros.inc"

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


// Current EL with SP0 handlers
.global sync_handler_current_el_sp0
sync_handler_current_el_sp0:
    save_context
    mov x0, sp
    bl Exception__handle_sync_current_sp0
    restore_context_preserve_x0
    eret

.global irq_handler_current_el_sp0
irq_handler_current_el_sp0:
    save_context
    mov x0, sp
    bl Exception__handle_irq_current_sp0
    restore_context
    eret

.global fiq_handler_current_el_sp0
fiq_handler_current_el_sp0:
    save_context
    mov x0, sp
    bl Exception__handle_fiq_current_sp0
    restore_context
    eret

.global serror_handler_current_el_sp0
serror_handler_current_el_sp0:
    save_context
    mov x0, sp
    bl Exception__handle_serror_current_sp0
    restore_context
    eret

// Current EL with SPx handlers
.global sync_handler_current_el_spx
sync_handler_current_el_spx:
    save_context
    mov x0, sp
    bl Exception__handle_sync_current_spx
    restore_context_preserve_x0
    eret

.global irq_handler_current_el_spx
irq_handler_current_el_spx:
    save_context
    mov x0, sp
    bl Exception__handle_irq_current_spx
    restore_context
    eret

.global fiq_handler_current_el_spx
fiq_handler_current_el_spx:
    save_context
    mov x0, sp
    bl Exception__handle_fiq_current_spx
    restore_context
    eret

.global serror_handler_current_el_spx
serror_handler_current_el_spx:
    save_context
    mov x0, sp
    bl Exception__handle_serror_current_spx
    restore_context
    eret

// Lower EL AArch64 handlers
.global sync_handler_lower_el_aarch64
sync_handler_lower_el_aarch64:
    save_context
    mov x0, sp
    bl Exception__handle_sync_lower_aarch64
    restore_context_preserve_x0
    eret

.global irq_handler_lower_el_aarch64
irq_handler_lower_el_aarch64:
    save_context

    mov x0, sp
    bl Exception__handle_irq_lower_aarch64
    restore_context
    eret

.global fiq_handler_lower_el_aarch64
fiq_handler_lower_el_aarch64:
    save_context
    mov x0, sp
    bl Exception__handle_fiq_lower_aarch64
    restore_context
    eret

.global serror_handler_lower_el_aarch64
serror_handler_lower_el_aarch64:
    save_context
    mov x0, sp
    bl Exception__handle_serror_lower_aarch64
    restore_context
    eret

// Lower EL AArch32 handlers
.global sync_handler_lower_el_aarch32
sync_handler_lower_el_aarch32:
    save_context
    mov x0, sp
    bl Exception__handle_sync_lower_aarch32
    restore_context_preserve_x0
    eret

.global irq_handler_lower_el_aarch32
irq_handler_lower_el_aarch32:
    save_context
    mov x0, sp
    bl Exception__handle_irq_lower_aarch32
    restore_context
    eret

.global fiq_handler_lower_el_aarch32
fiq_handler_lower_el_aarch32:
    save_context
    mov x0, sp
    bl Exception__handle_fiq_lower_aarch32
    restore_context
    eret

.global serror_handler_lower_el_aarch32
serror_handler_lower_el_aarch32:
    save_context
    mov x0, sp
    bl Exception__handle_serror_lower_aarch32
    restore_context
    eret
