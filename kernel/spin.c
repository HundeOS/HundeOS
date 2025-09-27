#include <stdint.h>
#include "spin.h"
#include "noop.h"

const uint32_t MAX = 100000000;

// Simple delay function
void spin(uint32_t count) {
    volatile uint32_t i;
    for (i = 0; i < count; i++) {
        noop();
    }
}

WaitResult spin_wait(spin_check check) {
    return spin_wait_max(check, MAX);
}

WaitResult spin_wait_max(spin_check check, const uint32_t max) {
    uint32_t count = 0;
    while (check())
    {
        if (count > max) {
            return ABORT;
        }
        noop();
        count++;
    }
    return OK;
}

WaitResult spin_wait_context(spin_context_check check, void * context, const uint32_t max) {
    uint32_t count = 0;
    while (check(context))
    {
        if (count > max) {
            return ABORT;
        }
        noop();
        count++;
    }
    return OK;
}

void wait_for_interrupts() {
    while (1) {
        cpu_wait();
    }
}