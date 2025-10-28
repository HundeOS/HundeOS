#include <stdint.h>

#include "boot.h"
#include "boot_debug.h"
#include "print.h"
#include "boot_strings.h"
#include "cpu.h"
#include "exceptions.h"
#include "mmu.h"

#include "../io/uart.h"

extern const uint64_t __k_stack_high[];
extern const uint64_t __k_stack_start[];
extern const uint64_t vector_table;

inline void enable_fp_simd(void) {
    uint64_t cpacr;

    // Read CPACR_EL1
    __asm__ volatile("mrs %0, cpacr_el1" : "=r" (cpacr));

    // Set FPEN bits [21:20] to 0b11 (no trapping of FP/SIMD)
    cpacr |= (3ULL << 20);

    // Write back
    __asm__ volatile("msr cpacr_el1, %0" : : "r" (cpacr));
    __asm__ volatile("isb");
}


__BOOT__(void) boot_uart_init()
{
    register unsigned int r;

    // initialize UART
    *P_UART0_CR = 0;           // turn off UART0

    *P_UART0_ICR = 0x7FF;      // clear interrupts
    *P_UART0_IBRD = 2;         // 115200 baud
    *P_UART0_FBRD = 0xB;
    *P_UART0_LCRH = 0b11 << 5; // 8n1
    *P_UART0_CR = 0x301;       // enable Tx, Rx, FIFO
}

__BOOT__(void) enable_tbi_for_kernel(void) {
    uint64_t tcr = rd_tcr();
    tcr |= (1ULL << 38);   // TBI1 = 1 (ignore top byte for TTBR1 region)
    // optional if you tag user pointers:
    // tcr |= (1ULL << 37); // TBI0 = 1
    wr_tcr(tcr);
}

__BOOT__(void) jump_to_kernel(uint64_t cpu)
{
    _print(new_line);
    _print(entering_hi_mem);
    _print_base(cpu, 10);
    _print(new_line);

    uint64_t base = (uint64_t)&__k_stack_high;
    uint64_t stack_top = (uint64_t)&__k_stack_start + (cpu * STACK_SIZE);
    enter_kernel(stack_top, (uint64_t)&vector_table, (uint64_t)&CPU__enter_kernel);
}

__BOOT__(void) common_init(void)
{
    _print(new_line);
    _print(booting);
}

__BOOT_STRING__(daif_) = "DAIF=";
__BOOT_STRING__(pmr_) = "PMR=";
__BOOT_STRING__(igrp_) = "IGRPEN1=";
__BOOT__(void) primary_booted()
{
    uint64_t daif, pmr, igrp;
    asm volatile("mrs %0, DAIF" : "=r"(daif));
    asm volatile("mrs %0, ICC_PMR_EL1" : "=r"(pmr));
    asm volatile("mrs %0, ICC_IGRPEN1_EL1" : "=r"(igrp));

    _print(daif_);
    _print_base(daif, 16);
    _print(new_line);
    _print(pmr_);
    _print_base(pmr, 16);
    _print(new_line);
    _print(igrp_);
    _print_base(igrp, 16);
    _print(new_line);
    _print(current_el);
    _print_base((read_current_el() >> 2), 10);
    _print(new_line);

    boot_uart_init();
    _print(new_line);
    _print(booting);
    _print(new_line);

    boot_vectors_init();

    setup_boot_mappings();
    enable_fp_simd();
    enable_tbi_for_kernel();
    enable_mmu();

    boot_secondary_cpus();

    jump_to_kernel(1);
}

__BOOT__(void) secondary_booted(uint64_t cpu)
{
    _print(new_line);
    _print(secondary_booting);
    _print_base(cpu, 10);
    _print(new_line);
    _print(new_line);

    boot_vectors_init();
    enable_fp_simd();
    enable_tbi_for_kernel();
    enable_mmu();

    jump_to_kernel(cpu);
}