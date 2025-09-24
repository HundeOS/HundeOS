#include <stdint.h>
#include "kernel/kernel.h"
#include "kernel/print.h"
#include "user/syscall.h"


// Call this first thing in your kernel_main or _start
// Main function with multiple debug approaches
void main(void) {
    arch_init();

    // Try simple UART output
    k_println("Hello from bootloader!");
    k_printf("System started with %i CPUs\n", cpu_count());

    k_printf("testing syscall: result=%i\n", syscall(99, 9, 8, 7));

    k_println("Test complete!");

    halt();
}