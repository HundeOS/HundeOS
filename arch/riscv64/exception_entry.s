    .section .text.exceptions, "ax"
    .align 2

    .equ CONTEXT_SIZE, 288
    .equ CONTEXT_SEPC, 256
    .equ CONTEXT_SSTATUS, 264
    .equ CONTEXT_SCAUSE, 272
    .equ CONTEXT_STVAL, 280
    .equ RISCV_SSTATUS_SPP, (1 << 8)
    .equ RISCV_SSTATUS_SUM, (1 << 18)
    .equ KERNEL_STACK_SIZE, 1048576

    .global Exception__entry
    .type Exception__entry, @function
Exception__entry:
    csrrw sp, sscratch, sp
    beqz sp, .Lsupervisor_trap

    addi sp, sp, -CONTEXT_SIZE
    sd zero, 0(sp)
    sd ra, 8(sp)
    sd t0, 40(sp)
    csrr t0, sscratch
    sd t0, 16(sp)
    csrw sscratch, zero
    li t0, RISCV_SSTATUS_SUM
    csrs sstatus, t0
    li t0, 1
    j .Lsave_context

.Lsupervisor_trap:
    csrrw sp, sscratch, sp
    addi sp, sp, -CONTEXT_SIZE
    sd zero, 0(sp)
    sd ra, 8(sp)
    sd t0, 40(sp)
    addi t0, sp, CONTEXT_SIZE
    sd t0, 16(sp)
    li t0, 0

.Lsave_context:
    sd gp, 24(sp)
    sd tp, 32(sp)
    sd t1, 48(sp)
    sd t2, 56(sp)
    beqz t0, .Lkernel_tp_ready
    la t1, __boot_stack_virtual_start
    sub t1, sp, t1
    li t2, KERNEL_STACK_SIZE
    divu t1, t1, t2
    slli t1, t1, 3
    la t2, RiscVBoot__stack_cpu_ids
    add t1, t1, t2
    ld tp, 0(t1)

.Lkernel_tp_ready:
    sd s0, 64(sp)
    sd s1, 72(sp)
    sd a0, 80(sp)
    sd a1, 88(sp)
    sd a2, 96(sp)
    sd a3, 104(sp)
    sd a4, 112(sp)
    sd a5, 120(sp)
    sd a6, 128(sp)
    sd a7, 136(sp)
    sd s2, 144(sp)
    sd s3, 152(sp)
    sd s4, 160(sp)
    sd s5, 168(sp)
    sd s6, 176(sp)
    sd s7, 184(sp)
    sd s8, 192(sp)
    sd s9, 200(sp)
    sd s10, 208(sp)
    sd s11, 216(sp)
    sd t3, 224(sp)
    sd t4, 232(sp)
    sd t5, 240(sp)
    sd t6, 248(sp)

    csrr t0, sepc
    sd t0, CONTEXT_SEPC(sp)
    csrr t0, sstatus
    sd t0, CONTEXT_SSTATUS(sp)
    csrr t0, scause
    sd t0, CONTEXT_SCAUSE(sp)
    csrr t0, stval
    sd t0, CONTEXT_STVAL(sp)

    mv a0, sp
    call Exception__handle

    ld t0, CONTEXT_SEPC(sp)
    csrw sepc, t0
    ld t0, CONTEXT_SSTATUS(sp)
    csrw sstatus, t0
    andi t0, t0, RISCV_SSTATUS_SPP
    bnez t0, .Lreturn_to_supervisor
    addi t0, sp, CONTEXT_SIZE
    csrw sscratch, t0
    j .Lrestore_context

.Lreturn_to_supervisor:
    csrw sscratch, zero

.Lrestore_context:
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

    .size Exception__entry, . - Exception__entry
