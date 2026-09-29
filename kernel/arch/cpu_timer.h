#ifndef __KERNEL_ARCH_CPU_TIMER_H_
#define __KERNEL_ARCH_CPU_TIMER_H_

#include <stdint.h>

#include "../../common/interrupts/interrupt_types.h"
#include "../system_info.h"

extern void CPUTimer__start(SystemInfoPtr system_info, uint64_t hz);
extern void CPUTimer__rearm(IRQNumber irq);
extern void CPUTimer__suspend(void);
extern void CPUTimer__resume(void);

#endif
