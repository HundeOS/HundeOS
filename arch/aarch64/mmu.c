#include <kernel.h>
#include "asm_helpers.h"
#include "mmu.h"

#include <stddef.h>

#include "page_table.h"

static PageTable * null_table_low_mem_table = NULL;

mutex_t init_mutex = MUTEX_INIT;

PageTable * MMU__init_null() {
    mutex_lock(&init_mutex);
    if (null_table_low_mem_table == NULL) {
        null_table_low_mem_table = PageTable__new();
    }
    mutex_unlock(&init_mutex);
    return null_table_low_mem_table;
}

#define GUARD_ASID  0xFFu  // reserved, never used by real processes

void MMU__park_ttbr0(uint64_t guard_L0_pa, uint16_t guard_asid, bool disable_walks) {
    if (disable_walks) {
        uint64_t tcr = read_tcr_el1();
        tcr |= (1ULL << 7);              // EPD0 = 1
        write_tcr_el1(tcr);
        dsb_ish();
        isb();
    }

    uint64_t ttbr0 = ((uint64_t)guard_asid << 48) | guard_L0_pa;
    write_ttbr0_el1(ttbr0);
    isb();
    // No TLBI needed if guard_asid != old_asid
}


void MMU__install_null_low_mem_table()
{
    PageTable * table = MMU__init_null();
    Address addr = PageTable__physical_address(table);

    MMU__park_ttbr0((uint64_t)addr, (GUARD_ASID << 48), false);
    __asm__ volatile("tlbi vmalle1");  // invalidate all EL1 stage-1 TLBs

}
