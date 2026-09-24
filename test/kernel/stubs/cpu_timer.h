#ifndef __TEST_KERNEL_STUBS_CPU_TIMER_H
#define __TEST_KERNEL_STUBS_CPU_TIMER_H

#include "../../../common/interrupts/interrupt_types.h"

void CPUTimerStub__reset(void);
uint64_t CPUTimerStub__rearm_count(void);
IRQNumber CPUTimerStub__last_irq(void);

#endif
