#ifndef __KERNEL_ARCH_CPU_H
#define __KERNEL_ARCH_CPU_H

#include <stdint.h>

typedef struct cpu cpu;

extern uint64_t CPU__id();

extern void CPU__noop();
extern void CPU__wait();
extern void CPU__yield();
extern void CPU__wait_for_interrupts();

extern void CPU__assign_id(uint64_t id);

#endif