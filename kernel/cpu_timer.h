#ifndef __KERNEL_CPU_TIMER_H
#define __KERNEL_CPU_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include "irq/irq_dispatcher.h"
#include "kernel_init.h"
#include "scheduler/scheduler.h"

uint64_t CPUTimer__interval(uint64_t frequency, uint64_t hz);
Result(IRQDispatchStatus) CPUTimer__handle_irq(IRQDispatchContextPtr context, KernelPtr kernel);

#endif
