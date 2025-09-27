#ifndef __KERNEL_CPU_TIMER_H
#define __KERNEL_CPU_TIMER_H

#include <stdint.h>

extern void start_cpu_timer(uint32_t hz);

void timer_handler();

#endif