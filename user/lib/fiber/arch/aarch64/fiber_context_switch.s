.section .text.fiber_context, "ax"

.global FiberContext__switch
.type FiberContext__switch, %function
FiberContext__switch:
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    str     x29, [x0, #80]
    mov     x2, sp
    str     x2, [x0, #88]
    str     x30, [x0, #96]
    str     d8, [x0, #112]
    str     d9, [x0, #120]
    str     d10, [x0, #128]
    str     d11, [x0, #136]
    str     d12, [x0, #144]
    str     d13, [x0, #152]
    str     d14, [x0, #160]
    str     d15, [x0, #168]

    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldr     x29, [x1, #80]
    ldr     x2, [x1, #88]
    ldr     x30, [x1, #96]
    ldr     x0, [x1, #104]
    ldr     d8, [x1, #112]
    ldr     d9, [x1, #120]
    ldr     d10, [x1, #128]
    ldr     d11, [x1, #136]
    ldr     d12, [x1, #144]
    ldr     d13, [x1, #152]
    ldr     d14, [x1, #160]
    ldr     d15, [x1, #168]
    mov     sp, x2
    ret
.size FiberContext__switch, . - FiberContext__switch
