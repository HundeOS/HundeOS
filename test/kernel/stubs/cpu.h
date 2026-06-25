#ifndef __TEST_KERNEL_STUBS_CPU_H
#define __TEST_KERNEL_STUBS_CPU_H

#include "../../../kernel/arch/cpu.h"

void reset_cpu_id();
void set_cpu_id(CPUID id);
void reset_cpu_count();
void set_cpu_count(uint64_t count);

#endif
