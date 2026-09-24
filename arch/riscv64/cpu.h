#ifndef HUND_RISCV64_CPU_H
#define HUND_RISCV64_CPU_H

#include <stdint.h>

#include "../../kernel/arch/cpu.h"

void RiscVCPU__configure_timebase(uint64_t frequency);
uint64_t RiscVCPU__physical_hart_id(void);
void CPU__set_boot_id(CPUID id);

#endif
