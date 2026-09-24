#include <stdint.h>

#include "boot.h"
#include "cpu.h"
#include "exceptions.h"
#include "mmu.h"

#include "boot_strings.h"
#include "print.h"
#include "../io/psci.h"

#include "boot_debug.h"

extern void (*secondary_boot_entry)(void);

__BOOT_ALLOCATION__(static uint8_t) __attribute__((aligned(16))) boot_cpu_stacks[MAX_CPUS][STACK_SIZE];

__BOOT_DATA__(static uint64_t) messaged = 1;

__BOOT__(void) boot_secondary_cpus(uint64_t cpu_count)
{
    boot_debug_print(master_cpu);
    boot_debug_print_base(cpu_id(), 10);
    boot_debug_print(new_line);
    boot_debug_print(start_secondary);

    // we count this cpu already online
    uint64_t entry_pa = (uint64_t)&secondary_boot_entry;

    if (cpu_count > MAX_CPUS)
    {
        cpu_count = MAX_CPUS;
    }

    for (uint64_t cpu = 1; cpu < cpu_count; ++cpu)
    {
        boot_debug_print(new_line);

        uintptr_t sp_top = (uintptr_t)&boot_cpu_stacks[cpu][STACK_SIZE];
        SMCCCRes r = smccc_hvc(PSCI_0_2_FN64_CPU_ON,
                               mpidr_for_cpu(cpu), // target MPIDR
                               entry_pa,           // entry PA
                               sp_top);            // context_id -> SP
        // SMCCCRes r = smc_call(PSCI_0_2_FN64_CPU_ON,
        //                        mpidr_for_cpu(cpu),  // target MPIDR
        //                        entry_pa,            // entry PA
        //                        sp_top);             // context_id -> SP
        if ((int64_t)r.x0 == PSCI_RET_SUCCESS || (int64_t)r.x0 == PSCI_RET_ALREADY_ON)
        {
            boot_debug_print(messaged_cpu);
            boot_debug_print_base(cpu + 1, 10);
            boot_debug_print(new_line);
            messaged++;
        }
    }
}

__BOOT__(void) cpu_booted()
{
    // Secondary CPUs arrive with translation disabled. Enable Normal-memory
    // mappings before any exclusive access, including the boot/logging locks.
    boot_vectors_init();
    enable_fp_simd();
    enable_tbi_for_kernel();
    enable_mmu();

    secondary_booted(rd_mpidr() & 0xff);
}
