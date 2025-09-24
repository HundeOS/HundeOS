#include "spin.h"
#include "debug.h"

void secondary_main(uint64_t cpu_id) {
    // set up GIC CPU interface, timers, etc.
    // then join your scheduler / worker loop
    __d_printf(INFO, "CPU entering main: %i\n", cpu_id);
    wait_for_interrupts();
}