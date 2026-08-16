#include <stdint.h>

#include "boot.h"

__BOOT__(void) enable_fp_simd(void) {
    uint64_t cpacr;

    // Read CPACR_EL1
    __asm__ volatile("mrs %0, cpacr_el1" : "=r" (cpacr));

    // Set FPEN bits [21:20] to 0b11 (no trapping of FP/SIMD)
    cpacr |= (3ULL << 20);

    // Write back
    __asm__ volatile("msr cpacr_el1, %0" : : "r" (cpacr));
    __asm__ volatile("isb");
}

__BOOT__(void) enable_tbi_for_kernel(void) {
    uint64_t tcr = rd_tcr();
    tcr |= (1ULL << 38);   // TBI1 = 1 (ignore top byte for TTBR1 region)
    // optional if you tag user pointers:
    // tcr |= (1ULL << 37); // TBI0 = 1
    wr_tcr(tcr);
}
