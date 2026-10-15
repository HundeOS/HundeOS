    .section .text.entry, "ax"
    .align 2
    .global _start

_start:
    mov x19, x0
    mov x20, x1
    mov x21, x2
    mov x22, x3

    ldr x9, =0x84000000
    ldr x10, =0x80108000
    ldr x11, =kernel_image_size
    ldr x11, [x11]
    add x12, x9, x11

.Lcopy_words:
    sub x13, x12, x9
    cmp x13, #8
    b.lo .Lcopy_bytes
    ldr x14, [x9], #8
    str x14, [x10], #8
    b .Lcopy_words

.Lcopy_bytes:
    cmp x9, x12
    b.hs .Lkernel_ready
    ldrb w14, [x9], #1
    strb w14, [x10], #1
    b .Lcopy_bytes

.Lkernel_ready:
    ldr x9, =0x80108000
    add x10, x9, x11
.Lclean_cache_line:
    dc cvau, x9
    add x9, x9, #64
    cmp x9, x10
    b.lo .Lclean_cache_line
    dsb ish

    ldr x9, =0x80108000
.Linvalidate_cache_line:
    ic ivau, x9
    add x9, x9, #64
    cmp x9, x10
    b.lo .Linvalidate_cache_line
    dsb ish
    isb

    mov x0, x19
    mov x1, x20
    mov x2, x21
    mov x3, x22
    ldr x9, =HUNDEOS_KERNEL_ENTRY
    br x9

    .section .rodata, "a"
    .align 3
kernel_image_size:
    .dword HUNDEOS_KERNEL_SIZE
