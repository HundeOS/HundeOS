#ifndef __KERNEL_ARCH_CPU_H
#define __KERNEL_ARCH_CPU_H

#include <stdint.h>

typedef uint64_t CPUID;
typedef struct CPU CPU;

extern CPUID CPU__id();

extern void CPU__noop();
extern void CPU__wait();
extern void CPU__yield();
extern void CPU__wait_for_interrupt();
extern void CPU__wait_for_event();
extern void CPU__check_halt();
extern void CPU__halt_all();

extern void CPU__assign_id(CPUID id);

#endif