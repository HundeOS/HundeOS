#include <stdint.h>

#include "spin.h"
#include "debug.h"
#include "arch/cpu.h"
#include "arch/cpu_timer.h"
#include "arch/panic.h"

extern void dummy();

void Kernel_main(uint64_t cpu_id) {
    __d_printf(DEBUG, "CPU entering main: %i\n", cpu_id);
    CPUTimer__start(10000);
    __d_printf(DEBUG, "Timer started: %i\n", cpu_id);
    //Process__spawn(&dummy);
    for (int i = 0; i < 100000000; i++) {
        if ((i == 100000000 / 2) && (cpu_id == 1)) {
            // causes a panic
            __d_printf(PROGRESS, "CPU %i starting: %p\n", *((uint64_t *) 0x40080000));
        }

    }
    CPU__wait_for_interrupts();
    Kernel__halt();
}