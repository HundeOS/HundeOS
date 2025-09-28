#ifndef __AARCH64_PAGE_TABLE_H
#define __AARCH64_PAGE_TABLE_H

#include <stdint.h>

#include "../kernel/kernel.h"
#include "page_alloc.h"

typedef uint64_t PageTableEntry;
#if !defined(__BYTE_ORDER__) || (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
# error "This PTE bitfield layout assumes little-endian (AArch64 Linux/QEMU style)."
#endif

#define TABLE_SIZE (PAGE_SIZE / sizeof(PageTableEntry))

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

void PageTable__grant(Process * self);
PageTable * PageTable__new();
PageTable * PageTable__ensure_next_level_table(PageTable * parent_level_table,
                                               uint64_t child_index);

PageTableEntry PageTable__make_table_desc(PageTable * next_level_page);

int PageTable__map_2M_block(PageTable * self,
                            uint64_t virtual_address,
                            uint64_t phys_addr,
                            MappingPermissions perms);

int PageTable__map_4k_page(PageTable * self,
                           uint64_t virtual_address,
                           uint64_t phys_addr,
                           MappingPermissions perms);

#endif