#ifndef __RISCV64_PAGE_TABLE_H
#define __RISCV64_PAGE_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "../../kernel/address.h"
#include "../../kernel/all_defs.h"
#include "../../kernel/arch/page_table.h"
#include "../../kernel/memory_page.h"

#define RISCV_SV39_PAGE_TABLE_ENTRIES (PAGE_SIZE / sizeof(RiscVPageTableEntry))
#define RISCV_SV39_USER_ROOT_ENTRIES (RISCV_SV39_PAGE_TABLE_ENTRIES / 2U)
#define RISCV_SV39_USER_ADDRESS_LIMIT (1ULL << 38U)

#define RISCV_SV39_LEVEL_2_INDEX(virtual_address) (((uint64_t)(virtual_address) >> 30U) & 0x1ffU)
#define RISCV_SV39_LEVEL_1_INDEX(virtual_address) (((uint64_t)(virtual_address) >> 21U) & 0x1ffU)
#define RISCV_SV39_LEVEL_0_INDEX(virtual_address) (((uint64_t)(virtual_address) >> 12U) & 0x1ffU)
#define RISCV_SV39_PTE_PPN_MASK ((1ULL << 44U) - 1U)

typedef uint64_t RiscVPageTableEntry;

typedef struct PageTable
{
    RiscVPageTableEntry entries[RISCV_SV39_PAGE_TABLE_ENTRIES];
} PageTable;

typedef enum RiscVPageTableEntryFlag
{
    RISCV_PTE_VALID = 1ULL << 0U,
    RISCV_PTE_READ = 1ULL << 1U,
    RISCV_PTE_WRITE = 1ULL << 2U,
    RISCV_PTE_EXECUTE = 1ULL << 3U,
    RISCV_PTE_USER = 1ULL << 4U,
    RISCV_PTE_GLOBAL = 1ULL << 5U,
    RISCV_PTE_ACCESSED = 1ULL << 6U,
    RISCV_PTE_DIRTY = 1ULL << 7U,
} RiscVPageTableEntryFlag;

typedef struct RiscVPagePermissions
{
    bool read;
    bool write;
    bool execute;
    bool user;
} RiscVPagePermissions;

typedef const RiscVPagePermissions * ConstRiscVPagePermissionsPtr;

static inline RiscVPagePermissions RiscVPagePermissions__new(bool read, bool write, bool execute, bool user)
{
    return (RiscVPagePermissions){
        .read = read,
        .write = write,
        .execute = execute,
        .user = user,
    };
}

static inline RiscVPageTableEntry RiscVPageTableEntry__table(Address physical_address)
{
    return (physical_address >> 2U) | RISCV_PTE_VALID;
}

static inline RiscVPageTableEntry RiscVPageTableEntry__leaf(Address physical_address,
                                                            ConstRiscVPagePermissionsPtr permissions,
                                                            uint64_t memory_attributes)
{
    RiscVPageTableEntry entry =
        (physical_address >> 2U) | RISCV_PTE_VALID | RISCV_PTE_ACCESSED | memory_attributes;
    if (permissions->read || permissions->write)
    {
        entry |= RISCV_PTE_READ;
    }
    if (permissions->write)
    {
        entry |= RISCV_PTE_WRITE | RISCV_PTE_DIRTY;
    }
    if (permissions->execute)
    {
        entry |= RISCV_PTE_EXECUTE;
    }
    if (permissions->user)
    {
        entry |= RISCV_PTE_USER;
    }
    return entry;
}

static inline bool RiscVPageTableEntry__is_table(RiscVPageTableEntry entry)
{
    uint64_t leaf_flags = RISCV_PTE_READ | RISCV_PTE_WRITE | RISCV_PTE_EXECUTE;
    return (entry & RISCV_PTE_VALID) != 0 && (entry & leaf_flags) == 0;
}

static inline Address RiscVPageTableEntry__physical_address(RiscVPageTableEntry entry)
{
    return ((entry >> 10U) & RISCV_SV39_PTE_PPN_MASK) << 12U;
}

static inline bool RiscVVirtualAddress__is_user(Address virtual_address)
{
    return virtual_address < RISCV_SV39_USER_ADDRESS_LIMIT;
}

static inline PageTablePtr PageTable__zero(PageTablePtr self)
{
    return (PageTablePtr)MemoryPage__zero((MemoryPagePtr)self);
}

#endif
