#include <stdint.h>
#include "spin.h"
#include "arch/cpu.h"

const uint64_t MAX = 100000000;

// Simple delay function
void Spin__spin(uint64_t count) {
    volatile uint64_t i;
    for (i = 0; i < count; i++) {
        CPU__noop();
    }
}

WaitResult Spin__wait(SpinCheck check) {
    return Spin__wait_max(check, MAX);
}

WaitResult Spin__wait_max(SpinCheck check, const uint64_t max) {

    for (uint64_t count = 0; check() && count < max; count++)
    {
        if (count > max) {
            return ABORT;
        }
        CPU__noop();
    }
    return OK;
}

WaitResult Spin__wait_context(SpinContextCheck check, SpinContext context, const uint64_t max) {
    uint64_t count = 0;
    while (check(context))
    {
        if (count > max) {
            return ABORT;
        }
        CPU__noop();
        count++;
    }
    return OK;
}
