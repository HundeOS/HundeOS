#ifndef _KERNEL_ARCH_INIT_H
#define _KERNEL_ARCH_INIT_H
#include <stdint.h>

#include "../kernel_init.h"

extern Result(SubsystemInitStatus) Arch__init();
extern Result(SubsystemInitStatus) Arch__enable_irq(uint64_t irq);
extern void Arch__mask_irq(uint64_t irq);
extern void Arch__unmask_irq(uint64_t irq);

#endif
