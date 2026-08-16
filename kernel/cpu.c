#include <stdint.h>
#include "cpu.h"

#include "mutex.h"
#include "debug.h"
#include "main.h"
#include "mmu.h"
#include "panic.h"
#include "arch/panic.h"
#include "boot.h"
#include "kernel_init.h"
#include "kernel_core.h"

volatile uint64_t cpus_online = 0;

static mutex_t startup_lock = MUTEX_INIT;

uint64_t CPU__count()
{
    return cpus_online;
}

// Called as each CPU enters the kernel from boot
void CPU__enter_kernel(SystemInfoPtr boot_system_info)
{
    mutex_lock(&startup_lock);
    cpus_online++;
    CPU__assign_id(cpus_online);
    CPUID cpu_id = CPU__id();
    if (cpu_id == 1)
    {
        Boot__init_bootstrap_system(boot_system_info);
    }
    __d_printf(PROGRESS, "CPU %i starting: %i\n", CPU__id(), cpus_online);
    mutex_unlock(&startup_lock);

    KernelPtr kernel = Kernel__instance();
    // remove the low memory mappings used to bootstrap
    MMU__install_null_low_mem_table(kernel);
    Kernel__boot();
    // If it returns, park
    Kernel__halt();
}
