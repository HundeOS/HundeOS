#include <stdint.h>
#ifndef __KERNEL_SPIN_H
#define __KERNEL_SPIN_H

typedef volatile uint32_t (*spin_check)();

void spin(uint32_t count);
void spin_wait(spin_check check);
void spin_wait_max(spin_check check, const uint32_t max);

#endif