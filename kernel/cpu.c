#include "cpu.h"
#include "mutex.h"
#include "debug.h"
#include "main.h"
#include "mmu.h"
#include "arch/panic.h"

volatile uint64_t cpus_online = 0;

static mutex_t startup_lock = MUTEX_INIT;

uint64_t CPU__count()
{
    return cpus_online;
}

// Called as each CPU enters the kernel from boot
void CPU__enter_kernel()
{
    mutex_lock(&startup_lock);
    cpus_online++;
    CPU__assign_id(cpus_online);
    __d_printf(INFO, "CPU %i starting: %i\n", CPU__id(), cpus_online);
    mutex_unlock(&startup_lock);
    // remove the low memory mappings used to bootstrap
    MMU__install_null_low_mem_table();
    Kernel_main(CPU__id());

    // If it returns, park
    Kernel__halt();
}
