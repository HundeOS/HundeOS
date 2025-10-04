// smp_boot.c
#include <stdint.h>
#include "cpu.h"
#include "io/psci.h"
#include "mem.h"
#include "../kernel/print.h"

#include "asm_helpers.h"
#include "../kernel/kernel.h"
#include "debug.h"
#include "exception.h"

static uint8_t __attribute__((aligned(16))) cpu_stacks[MAX_CPUS][STACK_SIZE];

inline void wr_vbar_el1(uint64_t v){ __asm__ __volatile__("msr VBAR_EL1, %0"::"r"(v)); __asm__ __volatile__("isb"); }

volatile uint64_t cpus_online = 0;

static mutex_t startup_lock = MUTEX_INIT;

extern void (* secondary_psci_entry)(void);

uint64_t CPU__id() {
    return cpu_id();
}

uint64_t CPU__count() {
    return cpus_online;
}

// Called as each CPU enters the kernel from boot
void cpu_enter_kernel()
{
    mutex_lock(&startup_lock);
    cpus_online++;
    cpu_set_this_id(cpus_online);
    __d_printf(INFO, "CPU %i starting: %i\n", cpu_id(), cpus_online);
    mutex_unlock(&startup_lock);
    cpu_main(cpu_id());

    // If it returns, park
    halt();
}
