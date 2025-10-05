#include "../kernel/kernel.h"
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

void MMU__install_null_low_mem_table()
{
    PageTable * table = MMU__init_null();
    Address addr = PageTable__physical_address(table);

    write_ttbr0_el1((uint64_t)addr);
}
