#include <stdint.h>
#include <stdatomic.h>
#include "cpu.h"

#include "debug.h"
#include "main.h"
#include "mmu.h"
#include "panic.h"
#include "arch/panic.h"
#include "boot.h"
#include "kernel_init.h"
#include "kernel_core.h"

static atomic_uint_fast64_t cpus_online = 0;
static atomic_bool bootstrap_ready = false;

uint64_t CPU__count()
{
    return atomic_load_explicit(&cpus_online, memory_order_relaxed);
}

// Called as each CPU enters the kernel from boot
void CPU__enter_kernel(SystemInfoPtr boot_system_info)
{
    CPUID cpu_id = CPU__id();
    if (cpu_id == 1)
    {
        Boot__init_bootstrap_system(boot_system_info);
        atomic_store_explicit(&bootstrap_ready, true, memory_order_release);
    }
    else
    {
        // A secondary may arrive before the primary has installed SystemInfo.
        while (!atomic_load_explicit(&bootstrap_ready, memory_order_acquire))
        {
            CPU__noop();
        }
    }
    uint64_t online = atomic_fetch_add_explicit(&cpus_online, 1, memory_order_relaxed) + 1;
    __d_printf(PROGRESS, "CPU %i starting: %i\n", cpu_id, online);

    KernelPtr kernel = Kernel__instance();
    // remove the low memory mappings used to bootstrap
    MMU__install_null_low_mem_table(kernel);
    Kernel__boot();
    // If it returns, park
    Kernel__halt();
}
