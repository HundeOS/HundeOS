#include <stddef.h>
#include <stdint.h>

#include "page_table.h"

#include "mmu.h"
#include "../../kernel/debug.h"
#include "../../kernel/kalloc.h"
#include "../../kernel/kernel_core.h"

#define PAGE_MASK   (~(PAGE_SIZE - 1ULL))
#define PAGE_OFF(x) ((uint64_t)(x) & (PAGE_SIZE - 1ULL))

// VA[47:39] = L0 index (9) - points to next table or a block (if block, rest of output addr is used as offset)
// VA[38:30] = L1 index (9) - points to next table or a block (if block, rest of output addr is used as offset)
// VA[29:21] = L2 index (9) - points to next table or a block (if block, rest of output addr is used as offset)
// VA[20:12] = L3 index (9) - points to next table or a block (if block, rest of output addr is used as offset)
// VA[11:0]  = page offset (12)

// Arm table entries look like this
// 63              55 54 53 52 51   50..48 47                        12 11 10 9 8 7 6 5 4 3 2 1 0
// +----------------+--+--+--+--+---+------+------------------------+--+--+-+-+-+---+-+-+-+---+-+
// |   (reserved)   |UXN|PXN|Contig| DBM | ignored |   OUTPUT_ADDR   |nG|AF|SH| |AP|NS|AttrIdx|P|V|
// +----------------+--+--+--+--+---+------+------------------------+--+--+-+-+-+---+-+-+-+---+-+
//                                                                                  ^   ^   ^  ^
//                                                                                  |   |   |  +-- Valid (1)
//                                                                                  |   |   +----- TableOrPage = 1 (PAGE)
//                                                                                  |   +--------- AttrIdx (MAIR slot 0..7)
//                                                                                  +------------- NS (non-secure, usually 0)
//

// 63                                                             0
// +-- … --+--UXN--+--PXN--+ DBM  Contig  |         Output address         |nG|AF| SH | AP |  AttrIndx |T|V
//          54      53      51     52            [for page: PA[47:12]]     11 10 [9:8][7:6]   [4:2]    1 0
//
// Legend
//  V (bit 0)          : Valid (1 = valid)
//  T (bit 1)          : 0 = Block (L1/L2), 1 = Page (L3)
//  AttrIndx[4:2]      : Memory attribute index (MAIR_EL1 slot 0..7)
//  AP[2:1]  (bits 7:6): Access permission (EL1/EL0 read/write)
//  SH[9:8]            : Shareability (00 NS, 10 Outer, 11 Inner)
//  AF (bit 10)        : Access flag (must be 1 or you’ll fault on access)
//  nG (bit 11)        : Non-global (usually 0 for kernel global mappings)
//  Output address     : Physical address (aligned; low 12 bits zero for 4 KiB pages)
//  Contig (bit 52)    : “Contiguous” hint (optional, usually 0)
//  DBM (bit 51)       : Dirty Bit Modifier (if you use hardware dirty/write-protect)
//  PXN (bit 53)       : Privileged eXecute-Never (blocks EL1 execute)
//  UXN (bit 54)       : Unprivileged eXecute-Never (blocks EL0 execute)


KernelAddress PageTable__new(KernelPtr kernel) {
    KernelAddress table = Kernel__alloc(PAGE_SIZE);
    __d_printf(DEBUG, "new table 0x%x", table);
    return table;
}

KernelAddress PageTable__empty(KernelPtr kernel)
{
    return PageTable__new(kernel);
}

KernelAddress PageTable__from_physical_address(KernelPtr kernel, Address physical)
{
    return Kernel__from_physical_address(kernel, physical);
}

KernelAddress PageTable__from_virtual_address(KernelPtr kernel, Address virtual_addr)
{
    return Kernel__from_virtual_address(kernel, virtual_addr);
}

void PageTable__map(PageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    Address user_start = align_down_4k(vaddr(address));
    Address phys_start = align_down_4k(u_kpaddr(address));
    Address user_end = align_up_4k(vaddr(address) + size);

    __d_printf(MMU, "Pagetable map uvirt=%p size=%p table=%p\n", vaddr(address), size, self);
    __d_printf(MMU, "Pagetable map start=%p phys=%p end=%p\n", user_start, phys_start, user_end);

    size_t adjusted_range = user_end - user_start;

    uint64_t pages = adjusted_range / PAGE_SIZE;
    pages += adjusted_range % PAGE_SIZE ? 1 : 0;

    AccessAttribute access = ACCESS_EL1_RO_EL0_RO;

    if (address.write)
    {
        access = ACCESS_EL1_RW_EL0_RW;
    }

    MappingPermissions perms = (MappingPermissions) {
        .memory_type = ATTR_INDEX_NORMAL,
        .shareability = SHARE_INNER,
        .access = access,
        .uxn = !address.execute,
        .pxn = 0
    };
    __d_printf(MMU, "Mapping pages = %i\n", pages);
    for (int page = 0; page < pages; page++)
    {
        Address user_addr = offset_addr(user_start, page * PAGE_SIZE);
        Address phys_addr = offset_addr(phys_start, page * PAGE_SIZE);

        PageTable__map_4k_page(self, kernel, user_addr, phys_addr, perms);
    }
    __asm__ __volatile__("dsb ishst; isb" ::: "memory");
    __d_println(MMU, "Mapped");
}

void PageTable__map_mmio(PageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    Address user_start = align_down_4k(vaddr(address));
    Address phys_start = align_down_4k(u_kpaddr(address));
    Address user_end = align_up_4k(user_start + size);
    size_t adjusted_range = user_end - user_start;
    uint64_t pages = adjusted_range / PAGE_SIZE;
    pages += adjusted_range % PAGE_SIZE ? 1 : 0;

    MappingPermissions perms = (MappingPermissions) {
        .memory_type = ATTR_INDEX_DEVICE,
        .shareability = SHARE_OUTER,
        .access = ACCESS_EL1_RW_EL0_RW,
        .uxn = 1,
        .pxn = 1
    };

    for (int page = 0; page < pages; page++)
    {
        Address user_addr = offset_addr(user_start, page * PAGE_SIZE);
        Address phys_addr = offset_addr(phys_start, page * PAGE_SIZE);
        PageTable__map_4k_page(self, kernel, user_addr, phys_addr, perms);
    }
    __asm__ __volatile__("dsb ishst; isb" ::: "memory");
}

static KernelAddress PageTable__next_level_table_if_present(KernelAddress parent_level_table,
                                                            KernelPtr kernel,
                                                            uint64_t child_index)
{
    PageTablePtr parent_level_table_ptr = v_ptr(parent_level_table, PageTable);
    uint64_t descriptor = parent_level_table_ptr->entries[child_index];

    if ((descriptor & FLAG_PTE_VALID) && (descriptor & FLAG_PTE_TABLE))
    {
        return PageTable__from_physical_address(kernel, descriptor & MASK_PTE_NEXTADDR);
    }

    return NULL_KERNEL_ADDRESS;
}

static void PageTable__invalidate_page(Address virtual_address)
{
    uint64_t page = virtual_address >> 12;
    __asm__ __volatile__(
        "dsb ishst\n"
        "tlbi vaae1is, %0\n"
        "dsb ish\n"
        "isb\n"
        :
        : "r"(page)
        : "memory"
    );
}

void PageTable__unmap(PageTablePtr self, KernelPtr kernel, Address virtual_address, size_t size)
{
    Address user_start = align_down_4k(virtual_address);
    Address user_end = align_up_4k(user_start + size);
    KernelAddress table = PageTable__from_virtual_address(kernel, addr(self));

    for (Address user_addr = user_start; user_addr < user_end; user_addr += PAGE_SIZE)
    {
        KernelAddress l1_tbl = PageTable__next_level_table_if_present(table, kernel, L0_IDX(user_addr));
        if (!l1_tbl.valid)
        {
            continue;
        }

        KernelAddress l2_tbl = PageTable__next_level_table_if_present(l1_tbl, kernel, L1_IDX(user_addr));
        if (!l2_tbl.valid)
        {
            continue;
        }

        KernelAddress l3_tbl = PageTable__next_level_table_if_present(l2_tbl, kernel, L2_IDX(user_addr));
        if (!l3_tbl.valid)
        {
            continue;
        }

        v_ptr(l3_tbl, PageTable)->entries[L3_IDX(user_addr)] = 0;
        PageTable__invalidate_page(user_addr);
    }
}

/**
 * ensure_next_level_table - Get (or create) the next-level page table for a slot.
 *
 * @parent_level_table:  Pointer to the current 512-entry table page (L0/L1/L2).
 * @child_index:         Index [0..511] within that table for the target VA.
 *
 * Returns:
 *   - pointer to the next-level table (L1/L2/L3) on success
 *   - NULL if we ran out of memory or the slot currently holds a leaf mapping
 *
 * Notes:
 *   - Each table page is 4 KiB with 512 x 8-byte descriptors (4 KiB granule).
 *   - If the slot already contains a valid TABLE descriptor, we just return it.
 *   - If the slot contains a valid LEAF (block/page) descriptor, we *do not*
 *     auto-split it here; the caller must handle splitting 1GiB/2MiB blocks
 *     into a table + leaves (and do proper TLB maintenance).
 *   - When installing a brand-new TABLE descriptor into *live* tables, the caller
 *     should ensure appropriate barriers/TLB maintenance before relying on HW walks
 *     (e.g., dsb ishst; isb; and invalidate if replacing something).
 */
KernelAddress PageTable__ensure_next_level_table(KernelAddress parent_level_table,
                                                 KernelPtr kernel,
                                                 uint64_t child_index)
{
    // Defensive: table has 512 entries at 4 KiB granule.
    // (Optional) if you have an assertion macro, enable it:
    // assert(child_index < 512);
    PageTablePtr parent_level_table_ptr = v_ptr(parent_level_table, PageTable);

    uint64_t descriptor = parent_level_table_ptr->entries[child_index];

    // Case 1: already a valid TABLE descriptor → return its next-level base.
    if ((descriptor & FLAG_PTE_VALID) && (descriptor & FLAG_PTE_TABLE)) {
        return PageTable__from_physical_address(kernel, descriptor & MASK_PTE_NEXTADDR);
    }

    // Case 2: slot holds a valid LEAF (block/page) → caller must split it.
    // We do not overwrite it here to avoid silent semantic changes.
    if ((descriptor & FLAG_PTE_VALID) && !(descriptor & FLAG_PTE_TABLE)) {
        __d_println(PANIC, "null table");
        return NULL_KERNEL_ADDRESS;  // leaf present; cannot "ensure" a table without a split
    }

    // Case 3: slot is invalid → allocate a fresh zeroed table page.
    KernelAddress next_level_table = Kernel__alloc(PAGE_SIZE);

    if (!next_level_table.valid) {
        __d_println(PANIC, "failed to allocate");
        return NULL_KERNEL_ADDRESS;  // we should probably cause a fault
    }

    // Install a TABLE descriptor that points to the new page.
    parent_level_table_ptr->entries[child_index] = PageTable__make_table_desc(p_ptr(next_level_table, PageTable));

    // If the parent table is already visible to hardware walks (i.e., live),
    // consider a store-release or an explicit barrier before returning:

    __asm__ __volatile__("dsb ishst; isb" ::: "memory");
    return next_level_table;
}

int PageTable__map_2M_block(PageTable * self,
                            KernelPtr kernel,
                            uint64_t virtual_address,
                            uint64_t phys_addr,
                            MappingPermissions perms)
{
    __d_printf(DEBUG, "Mapping 2M 0x%x\n", self);
    if (!self) {
        return -1;
    }
    __d_println(DEBUG, "Mapping 2M");
    KernelAddress table = PageTable__from_virtual_address(kernel, addr(self));
    KernelAddress l1_tbl = PageTable__ensure_next_level_table(table, kernel, L0_IDX(virtual_address));
    KernelAddress l2_tbl = PageTable__ensure_next_level_table(l1_tbl, kernel, L1_IDX(virtual_address));

    __d_printf(DEBUG, "Mapping 2M 0x%x\n", l2_tbl);
    v_ptr(l2_tbl, PageTable)->entries[L2_IDX(virtual_address)] =
        PageTable__l2_block_desc(phys_addr, perms.memory_type, perms.shareability, perms.access, perms.uxn, perms.pxn);

    return 0;
}

int PageTable__map_4k_page(PageTable * self,
                           KernelPtr kernel,
                           Address virtual_address,
                           Address phys_addr,
                           MappingPermissions perms)
{
    if (!self) {
        return -1;
    }

    KernelAddress table = PageTable__from_virtual_address(kernel, addr(self));
    KernelAddress l1_tbl = PageTable__ensure_next_level_table(table, kernel, L0_IDX(virtual_address));
    KernelAddress l2_tbl = PageTable__ensure_next_level_table(l1_tbl, kernel, L1_IDX(virtual_address));
    KernelAddress l3_tbl = PageTable__ensure_next_level_table(l2_tbl, kernel, L2_IDX(virtual_address));

    v_ptr(l3_tbl, PageTable)->entries[L3_IDX(virtual_address)] =
        PageTable__make_page_desc_4k_l3(
            phys_addr, perms.memory_type, perms.shareability, perms.access, perms.uxn, perms.pxn
        );
    return 0;
}
