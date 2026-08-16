#ifndef _KERNEL_ARCH_INIT_H
#define _KERNEL_ARCH_INIT_H
#include <stdint.h>

#include "../kernel_init.h"
#include "../system_info.h"

extern Result(SubsystemInitStatus) Arch__init(KernelPtr kernel);
extern Result(SubsystemInitStatus) Arch__enable_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq);
extern void Arch__mask_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq);
extern void Arch__unmask_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq);

#endif
