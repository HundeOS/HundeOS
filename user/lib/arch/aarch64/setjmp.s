.section .text.setjmp, "ax"

.global setjmp
.type setjmp, %function
setjmp:
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x1, sp
    str x1, [x0, #96]
    stp d8, d9, [x0, #104]
    stp d10, d11, [x0, #120]
    stp d12, d13, [x0, #136]
    stp d14, d15, [x0, #152]
    mov w0, #0
    ret
.size setjmp, . - setjmp

.global longjmp
.type longjmp, %function
longjmp:
    mov x2, x0
    cmp w1, #0
    csinc w0, w1, wzr, ne
    ldp x19, x20, [x2, #0]
    ldp x21, x22, [x2, #16]
    ldp x23, x24, [x2, #32]
    ldp x25, x26, [x2, #48]
    ldp x27, x28, [x2, #64]
    ldp x29, x30, [x2, #80]
    ldr x3, [x2, #96]
    ldp d8, d9, [x2, #104]
    ldp d10, d11, [x2, #120]
    ldp d12, d13, [x2, #136]
    ldp d14, d15, [x2, #152]
    mov sp, x3
    ret
.size longjmp, . - longjmp
