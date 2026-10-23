#include <stdint.h>

#include "mmu.h"
#include "page_table.h"

#include "../../kernel/kalloc.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/mutex.h"

#define RISCV_SATP_MODE_SV39 (8ULL << 60U)
#define RISCV_SATP_PPN_MASK ((1ULL << 44U) - 1U)

static PageTable null_table __attribute__((aligned(PAGE_SIZE)));
static mutex_t init_mutex = MUTEX_INIT;
static bool null_table_initialized = false;

static Address MMU__active_root_physical_address(void)
{
    uint64_t satp;
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return (satp & RISCV_SATP_PPN_MASK) << 12U;
}

static void MMU__install_root(Address physical_address)
{
    uint64_t satp = RISCV_SATP_MODE_SV39 | ((physical_address >> 12U) & RISCV_SATP_PPN_MASK);
    __asm__ volatile("fence rw, rw\n"
                     "csrw satp, %0\n"
                     "sfence.vma zero, zero"
                     :
                     : "r"(satp)
                     : "memory");
}

void MMU__init(void)
{
}

void MMU__install_null_low_mem_table(KernelPtr kernel)
{
    mutex_lock(&init_mutex);
    KernelAddress null_address = Kernel__from_virtual_address(kernel, addr(&null_table));
    if (null_table_initialized)
    {
        if (null_address.valid)
        {
            MMU__install_root(null_address.phys_addr);
        }
        mutex_unlock(&init_mutex);
        return;
    }

    PageTable__zero(&null_table);

    KernelAddress active_root = Kernel__from_physical_address(kernel, MMU__active_root_physical_address());
    if (active_root.valid)
    {
        PageTablePtr active_table = v_ptr(active_root, PageTable);
        for (uint64_t index = RISCV_SV39_USER_ROOT_ENTRIES; index < RISCV_SV39_PAGE_TABLE_ENTRIES; index++)
        {
            null_table.entries[index] = active_table->entries[index];
        }
    }

    uint64_t null_root_index = RISCV_SV39_LEVEL_2_INDEX(addr(&null_table));
    if (null_address.valid && null_root_index >= RISCV_SV39_USER_ROOT_ENTRIES &&
        null_table.entries[null_root_index] != 0)
    {
        null_table_initialized = true;
        MMU__install_root(null_address.phys_addr);
    }
    mutex_unlock(&init_mutex);
}

void MMU__install_low_mem_table(KernelAddress table)
{
    MMU__install_root(table.phys_addr);
}

void MMU__dereference_user_mappings(KernelPtr kernel, KernelAddress table)
{
    if (table.valid && MMU__active_root_physical_address() == table.phys_addr)
    {
        MMU__install_null_low_mem_table(kernel);
    }
}
