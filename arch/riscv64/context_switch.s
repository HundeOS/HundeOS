    .section .text.context, "ax"
    .align 2

    .equ RISCV_SATP_MODE_SV39, (8 << 60)
    .equ CONTEXT_SEPC, 256
    .equ CONTEXT_SSTATUS, 264

    .global RiscVProcess__switch_context
    .type RiscVProcess__switch_context, @function
RiscVProcess__switch_context:
    srli t0, a1, 12
    li t1, RISCV_SATP_MODE_SV39
    or t0, t0, t1
    csrw satp, t0
    sfence.vma zero, zero
    fence.i

    la t0, __boot_stack_virtual_start
    sub t1, sp, t0
    li t2, 1048576
    divu t1, t1, t2
    addi t1, t1, 1
    mul t1, t1, t2
    add t0, t0, t1
    csrw sscratch, t0
    mv sp, a0
    ld t0, CONTEXT_SEPC(sp)
    csrw sepc, t0
    ld t0, CONTEXT_SSTATUS(sp)
    csrw sstatus, t0

    ld ra, 8(sp)
    ld gp, 24(sp)
    ld tp, 32(sp)
    ld t0, 40(sp)
    ld t1, 48(sp)
    ld t2, 56(sp)
    ld s0, 64(sp)
    ld s1, 72(sp)
    ld a0, 80(sp)
    ld a1, 88(sp)
    ld a2, 96(sp)
    ld a3, 104(sp)
    ld a4, 112(sp)
    ld a5, 120(sp)
    ld a6, 128(sp)
    ld a7, 136(sp)
    ld s2, 144(sp)
    ld s3, 152(sp)
    ld s4, 160(sp)
    ld s5, 168(sp)
    ld s6, 176(sp)
    ld s7, 184(sp)
    ld s8, 192(sp)
    ld s9, 200(sp)
    ld s10, 208(sp)
    ld s11, 216(sp)
    ld t3, 224(sp)
    ld t4, 232(sp)
    ld t5, 240(sp)
    ld t6, 248(sp)
    ld sp, 16(sp)
    sret

    .size RiscVProcess__switch_context, . - RiscVProcess__switch_context

    .global RiscVProcess__switch_address_space
    .type RiscVProcess__switch_address_space, @function
RiscVProcess__switch_address_space:
    srli t0, a0, 12
    li t1, RISCV_SATP_MODE_SV39
    or t0, t0, t1
    csrw satp, t0
    sfence.vma zero, zero
    ret

    .size RiscVProcess__switch_address_space, . - RiscVProcess__switch_address_space
