#ifndef __AARCH64_BOOT_DEBUG_H_
#define __AARCH64_BOOT_DEBUG_H_

#include <stdint.h>
#include "boot.h"
#include "print.h"
#include "../page_table.h"

#define DESC_VALID (1ull << 0)
#define DESC_TABLE (1ull << 1) // 1=table, 0=block (at L1/L2)
#define PTE_AF (1ull << 10)

#define MASK_NEXTADDR 0x0000FFFFFFFFF000ull      // [47:12] for table/page
#define MASK_L1_BLOCK_PA 0x0000FFFFC0000000ull   // [47:30] .. 1 GiB blocks
#define MASK_L2_BLOCK_PA 0x0000FFFFFFFFE00000ull // [47:21] .. 2 MiB blocks

#ifndef ASSERT
#define ASSERT(cond, msg)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
            panic(msg);                                                                                                \
    } while (0)
#endif

__BOOT__(void) __dump_table(PageTablePtr table, int entries, const char * level);

__BOOT__(PageTablePtr) table_at(PageTablePtr table, unsigned index);

__BOOT__(void)
debug_table_pool(int entries, PageTablePtr low_mem_l0_table, PageTablePtr high_mem_l0_table, PageTable table_pool[],
                 int cursor);

__BOOT__(uint64_t) pa_bits_from_tcr_ips(uint64_t ips);

__BOOT__(uint64_t) read_pa_bits();

__BOOT__(uint64_t) clamp_pa_to_width(uint64_t pa);

__BOOT__(int)
va_to_pa_from_l0(const uint64_t * l0_root, uint64_t va, uint64_t * pa_out, int * leaf_level_out,
                 uint64_t * leaf_desc_out);

// Convenience: choose root by address band (48-bit canonical VA with T0SZ=T1SZ=16)
__BOOT__(const uint64_t *) pick_l0_for_va(uint64_t va, const uint64_t * ttbr0_l0, const uint64_t * ttbr1_l0);

// Example helper that prints/compares a symbol:
__BOOT__(uint64_t) check_symbol_va_to_pa(uint64_t va, const uint64_t * low_l0, const uint64_t * high_l0);

__BOOT__(uint64_t) va_to_pa_hw(uint64_t va);

__BOOT__(void) __debug_table(PageTablePtr table, uint64_t addr, const char * message, int entries);

#endif