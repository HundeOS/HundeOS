#ifndef __AARCH64_PAGE_TABLE_H
#define __AARCH64_PAGE_TABLE_H

#include <stdint.h>

#include "../../kernel/arch/page_table.h"
#include "../../kernel/page_alloc.h"
#include "../../kernel/address.h"
#include "../../kernel/all_defs.h"

#define TABLE_SIZE (PAGE_SIZE / sizeof(PageTableEntry))
#define L0_IDX(va)  (((uint64_t)(va) >> 39) & 0x1FF) // L0 is the top 9 bits of OUTPUT_ADDR
#define L1_IDX(va)  (((uint64_t)(va) >> 30) & 0x1FF) // L1 is the next 9 bits of OUTPUT_ADDR
#define L2_IDX(va)  (((uint64_t)(va) >> 21) & 0x1FF) // L2 is the next 9 bits of OUTPUT_ADDR
#define L3_IDX(va)  (((uint64_t)(va) >> 12) & 0x1FF) // L3 is the next 9 bits of OUTPUT_ADDR

typedef uint64_t PageTableEntry;

typedef struct PageTable {
    PageTableEntry entries[TABLE_SIZE];
} PageTable;

typedef enum {
    ATTR_INDEX_NORMAL = 0,   // MAIR slot for Normal WBWA
    ATTR_INDEX_DEVICE = 1,   // MAIR slot for Device-nGnRE (example)
} MairAttribute;

typedef enum {
    ATTR_NORMAL = 0xFFULL,   // MAIR slot for Normal WBWA
    ATTR_DEVICE = 0x04ULL,   // MAIR slot for Device-nGnRE (example)
} Mair;

typedef enum {
    SHIFT_PTE_ATTR_INDEX = 2,            // AttrIdx[2:0]
    SHIFT_PTE_ACCESS_PERMISSION = 6,            // AP[2:1]
    SHIFT_PTE_SHARE = 8,            // SH[1:0]
} PageTableEntryBitShifts;

typedef enum {
    // Single-bit flags
    FLAG_PTE_VALID = 1ull << 0,
    FLAG_PTE_TABLE = 1ull << 1,    // when VALID=1 and this bit=1 → table descriptor
    FLAG_PTE_PAGE = 1ULL << 1,
    FLAG_PTE_ACCESS = 1ull << 10,   // Access Flag
    FLAG_PTE_PRIV_EXECUTE_NEVER = 1ull << 53,   // Privileged eXecute-Never
    FLAG_PTE_UNPRIV_EXECUTE_NEVER = 1ull << 54,   // Unprivileged eXecute-Never
} PageTableEntryFlag;

typedef enum {
    /// convenience masks
    MASK_PTE_TYPE          = 1ull << 1,   // convenience mask for the type bit
    // Address field
    MASK_PTE_NEXTADDR  = 0x0000FFFFFFFFF000ULL,    // [47:12] next-level/table/PA bits

    // - L2 block (2 MiB) PA uses [47:21]
    MASK_PTE_L2_BLOCK_PA = 0x0000FFFFFFFFE00000ULL,

    MASK_PTE_ENSURE_2MB_ALIGNED = 0xFFFFFFFFFFE00000ULL,

    // (If you ever use L1 1 GiB blocks, PA uses [47:30]; mask shown for completeness)
    MASK_PTE_L1_BLOCK_PA   = 0x0000FFFFC0000000ULL
} PageTableEntryMask;

typedef enum {
    SHARE_NONE = (0u << SHIFT_PTE_SHARE),
    SHARE_OUTER   = (2u << SHIFT_PTE_SHARE),   // Outer Shareable
    SHARE_INNER   = (3u << SHIFT_PTE_SHARE),   // Inner Shareable
} ShareAttribute;


// AP encodings (RW for EL1/EL0)
//
// AP[2:1] = bits 7:6
//
// AP[2:1]  EL1 EL0
//      00  RW	—
//      01  RW  RW
//      10  RO  —
//      11  RO  RO
typedef enum {
    ACCESS_EL1_RW_EL0_NO = (0u << SHIFT_PTE_ACCESS_PERMISSION),
    ACCESS_EL1_RW_EL0_RW = (1u << SHIFT_PTE_ACCESS_PERMISSION),
    ACCESS_EL1_RO_EL0_NO = (2u << SHIFT_PTE_ACCESS_PERMISSION),
    ACCESS_EL1_RO_EL0_RO = (3u << SHIFT_PTE_ACCESS_PERMISSION),
} AccessAttribute;

typedef struct {
    MairAttribute memory_type;   // MAIR slot index (0..7)
    ShareAttribute shareability;  // SH_* value (already shifted)
    AccessAttribute access;        // AP_* value (already shifted)
    int uxn;           // 1 => EL0 cannot execute this page
    int pxn;           // 1 => EL1 cannot execute this page
} MappingPermissions;


int PageTable__map_2M_block(PageTable * self,
                            uint64_t virtual_address,
                            uint64_t phys_addr,
                            MappingPermissions perms);

int PageTable__map_4k_page(PageTable * self,
                           uint64_t virtual_address,
                           uint64_t phys_addr,
                           MappingPermissions perms);

/**
 * make_page_desc_4k - Build a 4 KiB leaf PTE (VA→PA) with given attributes.
 *
 * @phys_addr:      Physical address of the 4 KiB frame (low 12 bits ignored)
 * @attr_index:     MAIR slot index (0..7) selecting the memory type
 * @shareability:   Pre-shifted SH field (e.g., PTE_SH_IS / PTE_SH_OS / PTE_SH_NONE)
 * @access_perms:   Pre-shifted AP field (e.g., PTE_AP_EL1_RW_EL0_RW, _RO, etc.)
 * @uxn_enable:     1 to set UXN (EL0 cannot execute), 0 to allow EL0 execute
 * @pxn_enable:     1 to set PXN (EL1 cannot execute), 0 to allow EL1 execute
 */

inline PageTableEntry PageTable__make_page_desc_4k(Address phys_addr,
                                  MairAttribute mair_attribute,
                                  ShareAttribute shareability,   // already shifted
                                  AccessAttribute access_perms,   // already shifted
                                  int uxn_enable,
                                  int pxn_enable)
{
    uint64_t desc = FLAG_PTE_VALID | FLAG_PTE_ACCESS | FLAG_PTE_ACCESS
                  | (mair_attribute << SHIFT_PTE_ATTR_INDEX)
                  | (uint64_t)shareability
                  | (uint64_t)access_perms;

    if (uxn_enable) desc |= FLAG_PTE_UNPRIV_EXECUTE_NEVER;
    if (pxn_enable) desc |= FLAG_PTE_PAGE;

    desc |= (phys_addr & MASK_PTE_NEXTADDR);
    return desc;
}


inline PageTableEntry PageTable__make_page_desc_4k_l3(Address phys_addr,
                                  MairAttribute mair_index,
                                  ShareAttribute shareability,   // already shifted
                                  AccessAttribute access_perms,   // already shifted
                                  int uxn_enable,
                                  int pxn_enable)
{
    uint64_t desc = FLAG_PTE_VALID | FLAG_PTE_ACCESS | FLAG_PTE_PAGE
                  | (mair_index << SHIFT_PTE_ATTR_INDEX)
                  | (uint64_t)shareability
                  | (uint64_t)access_perms
                  | (phys_addr & MASK_PTE_NEXTADDR);

    if (uxn_enable) desc |= FLAG_PTE_UNPRIV_EXECUTE_NEVER;
    if (pxn_enable) desc |= FLAG_PTE_PRIV_EXECUTE_NEVER;

    return desc;
}

inline PageTableEntry PageTable__l2_block_desc(Address pa,
                                               MairAttribute attridx,
                                               ShareAttribute sh,
                                               AccessAttribute access,
                                               int uxn,
                                               int pxn)
{
    uint64_t desc = FLAG_PTE_VALID
                    | FLAG_PTE_ACCESS
                    | sh
                    | access
                    | (attridx << SHIFT_PTE_ATTR_INDEX);
    // Execute-never flags if not executable
    if (uxn) desc |= FLAG_PTE_UNPRIV_EXECUTE_NEVER;
    if (pxn) desc |= FLAG_PTE_PRIV_EXECUTE_NEVER;

    // PA[47:21] go into [47:21] of descriptor (block aligned at 2MiB)
    desc |= (pa & MASK_PTE_ENSURE_2MB_ALIGNED);
    return desc;
}


inline PageTable * PageTable__zero(PageTable * self)
{
    return (PageTable *) MemoryPage__zero((MemoryPage *) self);
}


inline PageTableEntry PageTable__make_table_desc(PageTable * next_level_page)
{
    return FLAG_PTE_VALID | FLAG_PTE_TABLE | ((uint64_t)next_level_page & MASK_PTE_NEXTADDR);
}

KernelAddress PageTable__from_virtual_address(Address virtual_addr);

#endif