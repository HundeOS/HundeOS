#include "../../../kernel/arch/mmu.h"
#include "../../../kernel/address.h"
#include "kalloc.h"

static KernelAddress low_mem_table = NULL_KERNEL_ADDRESS;
static KernelAddress high_mem_table = NULL_KERNEL_ADDRESS;
static uint64_t dereference_count;
static bool dereferenced_before_free;

void reset_mmu_state()
{
    low_mem_table = NULL_KERNEL_ADDRESS;
    high_mem_table = NULL_KERNEL_ADDRESS;
    dereference_count = 0;
    dereferenced_before_free = false;
}

void MMU__init()
{
    // do nothing
}

void MMU__install_null_low_mem_table(KernelPtr kernel)
{
    low_mem_table = NULL_KERNEL_ADDRESS;
}

void MMU__dereference_user_mappings(KernelPtr kernel, KernelAddress table)
{
    if (table.valid && low_mem_table.valid && table.phys_addr == low_mem_table.phys_addr)
    {
        dereference_count++;
        dereferenced_before_free = KAllocStub__is_allocated(table.virtual_addr);
        MMU__install_null_low_mem_table(kernel);
    }
}

KernelAddress MMUStub__active_table(void)
{
    return low_mem_table;
}

uint64_t MMUStub__dereference_count(void)
{
    return dereference_count;
}

bool MMUStub__dereferenced_before_free(void)
{
    return dereferenced_before_free;
}

void MMU__install_low_mem_table(KernelAddress address)
{
    low_mem_table = address;
}
