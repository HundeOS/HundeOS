#include "../../../kernel/arch/mmu.h"
#include "../../../kernel/address.h"

static KernelAddress low_mem_table = NULL_KERNEL_ADDRESS;
static KernelAddress high_mem_table = NULL_KERNEL_ADDRESS;

void reset_mmu_state()
{
    low_mem_table = NULL_KERNEL_ADDRESS;
    high_mem_table = NULL_KERNEL_ADDRESS;
}

void MMU__init()
{
    // do nothing
}

void MMU__install_null_low_mem_table(KernelPtr kernel)
{
}

void MMU__install_low_mem_table(KernelAddress address)
{
    low_mem_table = address;
}
