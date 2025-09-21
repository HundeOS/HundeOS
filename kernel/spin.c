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

void spin_wait(spin_check check) {
    spin_wait_max(check, MAX);
}

void spin_wait_max(spin_check check, const uint32_t max) {
    uint32_t count = 0;
    while (check())
    {
        if (count > max) {
            break;
        }
        noop();
        count++;
    }
        
}

