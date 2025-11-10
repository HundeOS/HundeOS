#ifndef __KERNEL_CPU_TIMER_H
#define __KERNEL_CPU_TIMER_H

#include <stdint.h>

#include "scheduler/scheduler.h"

extern void start_cpu_timer(uint32_t hz);

void timer_handler(CPUID cpu_id, SchedulerPtr scheduler, ProcessContextPtr context);

#endif