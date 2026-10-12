    .section .text.entry, "ax"
    .align 2
    .global _start

_start:
    mv s0, a0
    mv s1, a1

    li t0, 0x84000000
    li t1, 0x80200000
    la t2, kernel_image_size
    ld t2, 0(t2)
    add t3, t0, t2

.Lcopy_words:
    sub t4, t3, t0
    li t5, 8
    bltu t4, t5, .Lcopy_bytes
    ld t6, 0(t0)
    sd t6, 0(t1)
    addi t0, t0, 8
    addi t1, t1, 8
    j .Lcopy_words

.Lcopy_bytes:
    bgeu t0, t3, .Lkernel_ready
    lbu t6, 0(t0)
    sb t6, 0(t1)
    addi t0, t0, 1
    addi t1, t1, 1
    j .Lcopy_bytes

.Lkernel_ready:
    fence rw, rw
    fence.i
    mv a0, s0
    mv a1, s1
    li t0, 0x80200000
    jr t0

    .section .rodata, "a"
    .align 3
kernel_image_size:
    .dword HUNDEOS_KERNEL_SIZE
