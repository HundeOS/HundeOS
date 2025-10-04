#ifndef __AARCH64_CPU_H
#define __AARCH64_CPU_H

#include <stdint.h>
#include "process.h"
#include "cpu_common.h"

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
uint64_t start_secondary_cpus();
void start_primary_cpu();

uint64_t CPU__count();
uint64_t CPU__id();

inline uint64_t get_current_el(void) {
    uint64_t el;
    __asm__ __volatile__("mrs %0, CurrentEL" : "=r" (el));
    return (el >> 2) & 3;
}

inline unsigned cpu_aff0(void){ return (unsigned)(rd_mpidr() & 0xff); }

#endif