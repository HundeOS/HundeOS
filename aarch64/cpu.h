#ifndef __AARCH64_CPU_H
#define __AARCH64_CPU_H

#include <stdint.h>
#include "process.h"

typedef enum {
    CPU_OFFLINE = 0,
    CPU_BOOTING,
    CPU_ONLINE,
    CPU_IDLE
} cpu_state;

typedef struct cpu {
    Process* current_process;
    uint32_t cpu_id;
    void* kernel_stack;
    struct cpu* self;  // Pointer to itself
    cpu_state state;
    uint32_t mpidr;
} cpu;

void smp_init();
int start_secondary_cpus(uint64_t total_cpus);
void start_primary_cpu();

uint64_t cpu_count();
uint64_t cpu_id();

uint64_t get_current_el();

#endif