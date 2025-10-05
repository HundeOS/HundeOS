#include "spin.h"
#include "debug.h"
#include "arch/cpu.h"
#include "arch/cpu_timer.h"
#include "arch/panic.h"

extern void dummy();

void Kernel_main(uint64_t cpu_id) {
    __d_printf(INFO, "CPU entering main: %i\n", cpu_id);
    CPUTimer__start(10000);
    //Process__spawn(&dummy);
    __d_printf(INFO, "Timer started: %i\n", cpu_id);
    CPU__wait_for_interrupts();
    Kernel__halt();
}