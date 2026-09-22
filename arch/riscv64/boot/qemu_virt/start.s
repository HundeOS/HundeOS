    .section .text.entry, "ax"
    .align 2
    .global _start

_start:
    mv s0, a0
    mv s1, a1
    mv tp, a0
    la sp, __boot_stack_start
    li t0, 10485760
    add sp, sp, t0
    la t0, __boot_bss_start
    la t1, __boot_bss_end

.Lclear_boot_bss:
    bgeu t0, t1, .Lbss_ready
    sd zero, 0(t0)
    addi t0, t0, 8
    j .Lclear_boot_bss

.Lbss_ready:
    la t0, __kernel_bss_physical_start
    la t1, __kernel_bss_physical_end

.Lclear_kernel_bss:
    bgeu t0, t1, .Lmmu_ready
    sd zero, 0(t0)
    addi t0, t0, 8
    j .Lclear_kernel_bss

.Lmmu_ready:
    call RiscVMmu__enable_bootstrap

    li t1, -274877906944
    add sp, sp, t1
    mv a0, s0
    add a1, s1, t1
    la t0, __riscv_boot_main_physical
    add t0, t0, t1
    jr t0

.Lhalt:
    wfi
    j .Lhalt

    .global RiscVSecondary__entry
    .type RiscVSecondary__entry, @function
RiscVSecondary__entry:
    mv s0, a0
    mv s1, a1
    la sp, __boot_stack_start
    li t0, 10485760
    mul t0, t0, s1
    add sp, sp, t0
    call RiscVMmu__activate_bootstrap

    li t1, -274877906944
    add sp, sp, t1
    mv a0, s0
    mv a1, s1
    la t0, __riscv_boot_secondary_physical
    add t0, t0, t1
    jr t0

    .size RiscVSecondary__entry, . - RiscVSecondary__entry

    .section .boot.stack, "aw", @nobits
    .align 12
    .global __boot_stack_start
__boot_stack_start:
    .skip 167772160
    .global __boot_stack_top
__boot_stack_top:
