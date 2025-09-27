#include "cpu_timer.h"
#include "spin.h"
#include "debug.h"
#include "process.h"
#include "../user/syscall.h"

extern void dummy();

void cpu_main(uint64_t cpu_id) {
    // set up GIC CPU interface, timers, etc.
    // then join your scheduler / worker loop
    __d_printf(INFO, "CPU entering main: %i\n", cpu_id);
    start_cpu_timer(10000);
    //Process__spawn(&dummy);
    __d_printf(INFO, "Timer started: %i\n", cpu_id);
    wait_for_interrupts();
}