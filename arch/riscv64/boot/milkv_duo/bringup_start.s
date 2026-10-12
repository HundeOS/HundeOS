    .section .text.entry, "ax"
    .align 2
    .global _start

_start:
    mv s0, a0
    mv s1, a1
    la sp, __boot_stack_top
    la t0, __bss_start
    la t1, __bss_end

.Lclear_bss:
    bgeu t0, t1, .Lbss_ready
    sd zero, 0(t0)
    addi t0, t0, 8
    j .Lclear_bss

.Lbss_ready:
    mv a0, s0
    mv a1, s1
    call MilkVDuoBringup__main

.Lhalt:
    wfi
    j .Lhalt
