.section .text.setjmp_probe_registers, "ax"

.global SetjmpProbe__registers
.type SetjmpProbe__registers, %function
SetjmpProbe__registers:
    sub sp, sp, #336
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp d8, d9, [sp, #96]
    stp d10, d11, [sp, #112]
    stp d12, d13, [sp, #128]
    stp d14, d15, [sp, #144]

    mov x19, #19
    mov x20, #20
    mov x21, #21
    mov x22, #22
    mov x23, #23
    mov x24, #24
    mov x25, #25
    mov x26, #26
    mov x27, #27
    mov x28, #28
    mov x29, #29
    mov x9, #8
    fmov d8, x9
    mov x9, #9
    fmov d9, x9
    mov x9, #10
    fmov d10, x9
    mov x9, #11
    fmov d11, x9
    mov x9, #12
    fmov d12, x9
    mov x9, #13
    fmov d13, x9
    mov x9, #14
    fmov d14, x9
    mov x9, #15
    fmov d15, x9

    add x0, sp, #160
    bl setjmp
    cbnz w0, 1f

    mov x19, #0
    mov x20, #0
    mov x21, #0
    mov x22, #0
    mov x23, #0
    mov x24, #0
    mov x25, #0
    mov x26, #0
    mov x27, #0
    mov x28, #0
    mov x29, #0
    fmov d8, xzr
    fmov d9, xzr
    fmov d10, xzr
    fmov d11, xzr
    fmov d12, xzr
    fmov d13, xzr
    fmov d14, xzr
    fmov d15, xzr
    add x0, sp, #160
    mov w1, #9
    bl longjmp

1:
    cmp w0, #9
    b.ne 2f
    cmp x19, #19
    b.ne 2f
    cmp x20, #20
    b.ne 2f
    cmp x21, #21
    b.ne 2f
    cmp x22, #22
    b.ne 2f
    cmp x23, #23
    b.ne 2f
    cmp x24, #24
    b.ne 2f
    cmp x25, #25
    b.ne 2f
    cmp x26, #26
    b.ne 2f
    cmp x27, #27
    b.ne 2f
    cmp x28, #28
    b.ne 2f
    cmp x29, #29
    b.ne 2f
    fmov x10, d8
    cmp x10, #8
    b.ne 2f
    fmov x10, d9
    cmp x10, #9
    b.ne 2f
    fmov x10, d10
    cmp x10, #10
    b.ne 2f
    fmov x10, d11
    cmp x10, #11
    b.ne 2f
    fmov x10, d12
    cmp x10, #12
    b.ne 2f
    fmov x10, d13
    cmp x10, #13
    b.ne 2f
    fmov x10, d14
    cmp x10, #14
    b.ne 2f
    fmov x10, d15
    cmp x10, #15
    b.ne 2f
    mov w0, #0
    b 3f
2:
    mov w0, #1
3:
    ldp d8, d9, [sp, #96]
    ldp d10, d11, [sp, #112]
    ldp d12, d13, [sp, #128]
    ldp d14, d15, [sp, #144]
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #336
    ret
.size SetjmpProbe__registers, . - SetjmpProbe__registers
