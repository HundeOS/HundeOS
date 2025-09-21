#include <stdint.h>
#include "rasppi/io/uart.h"
#include "exception.h"
#include "mem.h"

static void enable_fp_simd(void) {
    uint64_t cpacr;
    
    // Read CPACR_EL1
    __asm__ volatile("mrs %0, cpacr_el1" : "=r" (cpacr));
    
    // Set FPEN bits [21:20] to 0b11 (no trapping of FP/SIMD)
    cpacr |= (3ULL << 20);
    
    // Write back
    __asm__ volatile("msr cpacr_el1, %0" : : "r" (cpacr));
    __asm__ volatile("isb");
}


void arch_init() {
    enable_fp_simd();
    uart_init();
    vectors_init();
    setup_mmu();
}