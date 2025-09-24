// smp_boot.c
#include <stdint.h>
#include "psci.h"
#include "mem.h"
#include "../kernel/print.h"

#define MAX_CPUS 8
#define STACK_SIZE (16*1024)

#include "../kernel/kernel.h"
#include "debug.h"

static uint8_t __attribute__((aligned(16))) cpu_stacks[MAX_CPUS][STACK_SIZE];

// we count the primary
volatile uint64_t cpus_online = 1;

static mutex_t startup_lock = MUTEX_INIT;

extern void secondary_psci_entry(void);
extern void secondary_main(uint64_t cpu_id);


uint64_t cpu_id() {
    uint64_t v;
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r"(v));
    return v;
}

uint64_t cpu_count() {
    return cpus_online;
}

static inline void cpu_set_this_id(unsigned id){
    __asm__ __volatile__("msr tpidr_el1, %0" :: "r"((uint64_t)id) : "memory");
}

// Called on each secondary by the assembly entry
void start_cpu()
{
    // Turn MMU on with the same tables the boot core installed
    mmu_secondary_init();

    mutex_lock(&startup_lock);
    cpus_online++;
    __d_printf(INFO, "CPU starting: %i\n", cpus_online);
    cpu_set_this_id(cpus_online);
    mutex_unlock(&startup_lock);

    // Jump to your per-CPU code
    secondary_main(cpu_id());

    // If it returns, park
    halt();
}

// Simple helper: on QEMU virt, MPIDR.AFF0 == logical cpu#
static inline uint64_t mpidr_for_cpu(unsigned cpu) { return (uint64_t)cpu; }

uint32_t check_cpus_online(void * num) {
    unsigned messaged = *(unsigned *)num;
    return cpus_online != messaged;
}

uint64_t start_secondary_cpus()
{
    __d_printf(DEBUG, "Master cpu=%u\n", cpu_id());
    __d_println(DEBUG, "start_secondary_cpus");

    // we count this cpu already online
    uint64_t messaged = 1;

    uintptr_t entry_pa = (uintptr_t)secondary_psci_entry; // must be PA; we identity-map
    for (unsigned cpu = 1; cpu < MAX_CPUS; ++cpu) {
        // per-CPU stack top (16-byte aligned)
        uintptr_t sp_top = (uintptr_t)&cpu_stacks[cpu][STACK_SIZE];
        // struct smccc_res r = smc_call(PSCI_0_2_FN64_CPU_ON,
        //                               mpidr_for_cpu(cpu),  // target MPIDR
        //                               entry_pa,            // entry point PA
        //                               sp_top);             // context_id -> initial SP
        struct smccc_res r = smccc_hvc(PSCI_0_2_FN64_CPU_ON,
                                       mpidr_for_cpu(cpu),  // target MPIDR
                                       entry_pa,            // entry PA
                                       sp_top);             // context_id -> SP
        if ((int64_t)r.x0 == PSCI_RET_SUCCESS || (int64_t)r.x0 == PSCI_RET_ALREADY_ON) {
            __d_printf(DEBUG, "messaged cpu %i\n", cpu + 1);
            messaged++;
        }
    }

    __d_printf(DEBUG, "waiting for %u cpus to come alive\n", messaged);
    // Optionally wait for them to come online

    if (spin_wait_context(&check_cpus_online, &messaged, 100000) == ABORT) {
        __d_println(DEBUG, "Aborted waiting for CPUS to come online");
    }
    __d_printf(INFO, "CPUS online %u\n", cpus_online);
    return cpus_online;
}

uint64_t get_current_el(void) {
    uint64_t el;
    __asm__ __volatile__("mrs %0, CurrentEL" : "=r" (el));
    return (el >> 2) & 3;
}
