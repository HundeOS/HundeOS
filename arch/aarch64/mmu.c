#include "../../kernel/address.h"
#include "../../kernel/mutex.h"
#include "../../kernel/address.h"
#include "asm_helpers.h"
#include "mmu.h"

#include <stddef.h>

#include "page_table.h"

static PageTable null_table;

mutex_t init_mutex = MUTEX_INIT;

PageTablePtr MMU__init_null()
{
    mutex_lock(&init_mutex);

    PageTable__zero(&null_table);
    mutex_unlock(&init_mutex);
    return &null_table;
}

#define GUARD_ASID 0xFFu // reserved, never used by real processes

void MMU__park_ttbr0(uint64_t guard_L0_pa, uint16_t guard_asid, bool disable_walks)
{
    if (disable_walks)
    {
        uint64_t tcr = read_tcr_el1();
        tcr |= (1ULL << 7); // EPD0 = 1
        write_tcr_el1(tcr);
        dsb_ish();
        isb();
    }

    uint64_t ttbr0 = ((uint64_t)guard_asid << 48) | guard_L0_pa;
    write_ttbr0_el1(ttbr0);
    isb();
    // No TLBI needed if guard_asid != old_asid
}

void MMU__install_null_low_mem_table(KernelPtr kernel)
{
    PageTablePtr table = MMU__init_null();
    KernelAddress address = PageTable__from_virtual_address(kernel, addr(table));

    MMU__park_ttbr0(address.phys_addr, (GUARD_ASID << 48), false);
    __asm__ volatile("tlbi vmalle1"); // invalidate all EL1 stage-1 TLBs
}
