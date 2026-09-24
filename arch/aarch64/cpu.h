#ifndef __AARCH64_CPU_H
#define __AARCH64_CPU_H

#include <stdint.h>
#include "../../kernel/process.h"
#include "../../kernel/arch/cpu.h"

typedef enum
{
    CPU_OFFLINE = 0,
    CPU_BOOTING,
    CPU_ONLINE,
    CPU_IDLE
} cpu_state;

typedef struct CPU
{
    ProcessPtr current_process;
    uint64_t cpu_id;
    Address kernel_stack;
    cpu_state state;
    uint64_t mpidr;
} CPU;

void smp_init();
uint64_t start_secondary_cpus();
void start_primary_cpu();
void CPU__set_boot_id(CPUID id);

inline uint64_t get_current_el(void)
{
    uint64_t el;
    __asm__ __volatile__("mrs %0, CurrentEL" : "=r"(el));
    return (el >> 2) & 3;
}

#endif
