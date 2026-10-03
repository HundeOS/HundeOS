.section .text.setjmp_probe_registers, "ax"

.global SetjmpProbe__registers
.type SetjmpProbe__registers, @function
SetjmpProbe__registers:
    addi sp, sp, -224
    sd ra, 0(sp)
    sd s0, 8(sp)
    sd s1, 16(sp)
    sd s2, 24(sp)
    sd s3, 32(sp)
    sd s4, 40(sp)
    sd s5, 48(sp)
    sd s6, 56(sp)
    sd s7, 64(sp)
    sd s8, 72(sp)
    sd s9, 80(sp)
    sd s10, 88(sp)
    sd s11, 96(sp)

    li s0, 10
    li s1, 11
    li s2, 12
    li s3, 13
    li s4, 14
    li s5, 15
    li s6, 16
    li s7, 17
    li s8, 18
    li s9, 19
    li s10, 20
    li s11, 21

    addi a0, sp, 112
    call setjmp
    bnez a0, 1f

    li s0, 0
    li s1, 0
    li s2, 0
    li s3, 0
    li s4, 0
    li s5, 0
    li s6, 0
    li s7, 0
    li s8, 0
    li s9, 0
    li s10, 0
    li s11, 0
    addi a0, sp, 112
    li a1, 9
    call longjmp

1:
    li t0, 9
    bne a0, t0, 2f
    li t0, 10
    bne s0, t0, 2f
    li t0, 11
    bne s1, t0, 2f
    li t0, 12
    bne s2, t0, 2f
    li t0, 13
    bne s3, t0, 2f
    li t0, 14
    bne s4, t0, 2f
    li t0, 15
    bne s5, t0, 2f
    li t0, 16
    bne s6, t0, 2f
    li t0, 17
    bne s7, t0, 2f
    li t0, 18
    bne s8, t0, 2f
    li t0, 19
    bne s9, t0, 2f
    li t0, 20
    bne s10, t0, 2f
    li t0, 21
    bne s11, t0, 2f
    li a0, 0
    j 3f
2:
    li a0, 1
3:
    ld ra, 0(sp)
    ld s0, 8(sp)
    ld s1, 16(sp)
    ld s2, 24(sp)
    ld s3, 32(sp)
    ld s4, 40(sp)
    ld s5, 48(sp)
    ld s6, 56(sp)
    ld s7, 64(sp)
    ld s8, 72(sp)
    ld s9, 80(sp)
    ld s10, 88(sp)
    ld s11, 96(sp)
    addi sp, sp, 224
    ret
.size SetjmpProbe__registers, . - SetjmpProbe__registers
