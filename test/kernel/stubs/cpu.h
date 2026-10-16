#ifndef __TEST_KERNEL_STUBS_CPU_H
#define __TEST_KERNEL_STUBS_CPU_H

#include "../../../kernel/arch/cpu.h"

void reset_cpu_id();
void set_cpu_id(CPUID id);
void reset_cpu_count();
void set_cpu_count(uint64_t count);
void reset_cpu_reschedule(void);
CPUID CPUStub__rescheduled_cpu(void);
uint64_t CPUStub__reschedule_count(void);
void CPUStub__reset_wait_count(void);
uint64_t CPUStub__wait_count(void);

#endif
