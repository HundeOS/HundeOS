#include "cpu.h"

static CPUID boot_cpu_id;

CPUID CPU__id(void)
{
    uint64_t hart_id;
    __asm__ volatile("mv %0, tp" : "=r"(hart_id));
    return hart_id;
}

void CPU__set_boot_id(CPUID id)
{
    boot_cpu_id = id;
}

bool CPU__is_boot_cpu(void)
{
    return CPU__id() == boot_cpu_id;
}

uint64_t RiscVCPU__physical_hart_id(void)
{
    return CPU__id();
}

void CPU__assign_id(CPUID id)
{
    __asm__ volatile("mv tp, %0" : : "r"(id));
}
