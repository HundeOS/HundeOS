#include <stdint.h>
#include "kernel/kernel.h"
#include "kernel/print.h"
#include "user/syscall.h"

#include "aarch64/rasppi/io/uart.h"

// Call this first thing in your kernel_main or _start
// Main function with multiple debug approaches
void main(void) {
    arch_init();

    // Try simple UART output
    k_println("Hello from ARM bootloader!");
    
    k_println("testing syscall:");
    uint64_t result = syscall(99, 9, 8, 7);
    k_printf("result=%i\n", result);

    k_println("done");
    k_println("Test complete!");

    halt();
}