#ifndef __KERNEL_ARCH_CPU_H
#define __KERNEL_ARCH_CPU_H

#include <stdint.h>

#include "init_code.h"
#include "../types/result.h"
#include "../system_info.h"

typedef uint64_t CPUID;

ResultDef(CPUID);

typedef struct CPU CPU;

extern CPUID CPU__id();
bool CPU__is_boot_cpu(void);
uint64_t CPU__time_usec(void);

extern void CPU__noop();
extern void CPU__wait();
extern void CPU__yield();
extern void CPU__wait_for_interrupt();
extern void CPU__wait_for_event();
extern void CPU__signal_event();
void CPU__request_reschedule(CPUID cpu);
extern void CPU__check_halt();
extern void CPU__halt_all();

void CPU__halt_forever(void) __attribute__((noreturn));

extern Result(InitCode) CPU__init(SystemInfoPtr system_info);

extern void CPU__assign_id(CPUID id);

#endif
