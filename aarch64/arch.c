#include <stdint.h>
#include "../kernel/kernel.h"
#include "io/uart.h"
#include "exception.h"
#include "mem.h"
#include "cpu.h"
#include "debug.h"

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
    __d_println(PROGRESS, "Initialising aarch64 system");
    enable_fp_simd();
    __d_println(DEBUG, "SIMD enabled");
    uart_init();
    __d_println(DEBUG, "UART initialised");
    vectors_init();
    __d_println(DEBUG, "Vectors initialised");
    MMU__init();
    __d_println(DEBUG, "MMU initialised");
    start_secondary_cpus(4);
    __d_println(PROGRESS, "SMP initialised");
    start_primary_cpu();
}