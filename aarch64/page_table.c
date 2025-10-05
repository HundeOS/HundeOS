#include <stddef.h>
#include <stdint.h>

#include "../kernel/kernel.h"
#include "page_table.h"

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

PageTable * PageTable__new() {
    PageTable * table = (PageTable *) MemoryPage__alloc();
    __d_printf(DEBUG, "new table 0x%x", table);
    return table;
}

PageTable * PageTable__empty() {
    return PageTable__new();
}

Address PageTable__physical_address(PageTable * self) {
    KernelAddress address = MemoryPage__address((MemoryPage *) self);
    return address.phys_addr;
}

/* ---- VA→PA using AT+PAR: translate an EL1 (kernel) VA to a physical address ---- */
inline int PageTable__el1_va_to_phys(uint64_t kernel_va) {
    uint64_t par;
    __asm__ __volatile__(
        "at s1e1r, %1\n"
        "isb\n"
        "mrs %0, par_el1\n"
        : "=r"(par) : "r"(kernel_va) : "memory");

    if (par & 1) {
        Kernel__panic("failed to determine physical address", NULL);
    }
    return (par & MASK_PTE_NEXTADDR) | (kernel_va & 0xFFF);
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
PageTable * PageTable__ensure_next_level_table(PageTable * parent_level_table,
                                               uint64_t child_index)
{
    // Defensive: table has 512 entries at 4 KiB granule.
    // (Optional) if you have an assertion macro, enable it:
    // assert(child_index < 512);

    uint64_t descriptor = parent_level_table->entries[child_index];

    // Case 1: already a valid TABLE descriptor → return its next-level base.
    if ((descriptor & FLAG_PTE_VALID) && (descriptor & FLAG_PTE_TABLE)) {
        return (PageTable *)(descriptor & MASK_PTE_NEXTADDR);
    }

    // Case 2: slot holds a valid LEAF (block/page) → caller must split it.
    // We do not overwrite it here to avoid silent semantic changes.
    if ((descriptor & FLAG_PTE_VALID) && !(descriptor & FLAG_PTE_TABLE)) {
        return NULL;  // leaf present; cannot "ensure" a table without a split
    }

    // Case 3: slot is invalid → allocate a fresh zeroed table page.
    PageTable * next_level_table = (PageTable *) MemoryPage__alloc();

    if (!next_level_table) {
        return NULL;  // we should probably cause a fault
    }

    // Install a TABLE descriptor that points to the new page.
    parent_level_table->entries[child_index] = PageTable__make_table_desc(next_level_table);

    // If the parent table is already visible to hardware walks (i.e., live),
    // consider a store-release or an explicit barrier before returning:

    __asm__ __volatile__("dsb ishst; isb" ::: "memory");
    return next_level_table;
}

int PageTable__map_2M_block(PageTable * self,
                            uint64_t virtual_address,
                            uint64_t phys_addr,
                            MappingPermissions perms)
{
    __d_printf(DEBUG, "Mapping 2M 0x%x\n", self);
    if (!self) {
        return -1;
    }
    __d_println(DEBUG, "Mapping 2M");
    PageTable * l1_tbl = PageTable__ensure_next_level_table(self, L0_IDX(virtual_address));
    PageTable * l2_tbl = PageTable__ensure_next_level_table(l1_tbl, L1_IDX(virtual_address));

    __d_printf(DEBUG, "Mapping 2M 0x%x\n", l2_tbl);
    l2_tbl->entries[L2_IDX(virtual_address)] = PageTable__l2_block_desc(phys_addr,
                                                                        perms.memory_type,
                                                                        perms.shareability,
                                                                        perms.access,
                                                                        perms.uxn,
                                                                        perms.pxn);

    return 0;
}

int PageTable__map_4k_page(PageTable * self,
                           uint64_t virtual_address,
                           uint64_t phys_addr,
                           MappingPermissions perms)
{
    if (!self) {
        return -1;
    }

    PageTable * l1_tbl = PageTable__ensure_next_level_table(self, L0_IDX(virtual_address));
    PageTable * l2_tbl = PageTable__ensure_next_level_table(l1_tbl, L1_IDX(virtual_address));
    PageTable * l3_tbl = PageTable__ensure_next_level_table(l2_tbl, L2_IDX(virtual_address));

    l3_tbl->entries[L3_IDX(virtual_address)] = PageTable__make_page_desc_4k_l3(phys_addr,
                                                                               perms.memory_type,
                                                                               perms.shareability,
                                                                               perms.access,
                                                                               perms.uxn,
                                                                               perms.pxn);
    return 0;
}


/* ---- Convenience: map an existing kernel VA into a process at a chosen EL0 VA ---- */
inline int PageTable__map_kernel_va_into_process(PageTable * process_l0_root,
                                                 uint64_t kernel_va_el1,  // source VA in kernel space
                                                 uint64_t user_va_el0,    // destination VA in user space
                                                 MappingPermissions perms,
                                                 uint16_t asid_for_tlb)   // process ASID (for TLB shootdown)
{
    uint64_t phys = PageTable__el1_va_to_phys(kernel_va_el1);

    if (PageTable__map_4k_page(process_l0_root, user_va_el0, phys, perms) != 0)
        return -1;

    // If the process might already be running, invalidate its TLB entries for this ASID.
    __asm__ __volatile__("tlbi aside1, %0" :: "r"((uint64_t)asid_for_tlb) : "memory");
    __asm__ __volatile__("dsb ish; isb");
    return 0;
}

/* VA→PA translation (EL1) using AT+PAR. Returns 0 on success. */
inline int PageTable__el1_va_to_phys_pagebase(const void *kva_page_base, uint64_t *out_pa_page_base) {
    uint64_t par;
    __asm__ __volatile__(
        "at s1e1r, %1\n"
        "isb\n"
        "mrs %0, par_el1\n"
        : "=r"(par) : "r"(kva_page_base) : "memory");
    if (par & 1) return -1;  // translation fault
    *out_pa_page_base = par & 0x0000FFFFFFFFF000ULL; // PA[51:12]
    return 0;
}

/**
 * map_kernel_range_into_process - map a kernel VA range into a process at EL0.
 *
 * @process_l0_root:  process TTBR0 L0 root (512-entry table)
 * @kernel_base_el1:  starting EL1 (kernel) VA of the source range
 * @user_base_el0:    starting EL0 VA where the range should appear in the process
 * @word_count64:     length of the range in 64-bit words (bytes = words*8)
 * @perms:            mapping attributes (MAIR index / SH / AP / UXN / PXN)
 * @asid_for_tlb:     process ASID for TLB maintenance
 *
 * Requirements:
 *   - kernel_base_el1 and user_base_el0 must have the **same 4 KiB offset**
 *     (PAGE_OFF(kernel_base_el1) == PAGE_OFF(user_base_el0)).
 *
 * Returns 0 on success, <0 on error.
 */
inline int PageTable__map_kernel_range_into_process(PageTable * process_l0_root,
                                                    const void * kernel_base_el1,
                                                    size_t size,
                                                    uint64_t user_base_el0,
                                                    MappingPermissions perms,
                                                    uint16_t asid_for_tlb)
{
    if (!process_l0_root || !kernel_base_el1) return -1;
    uint64_t bytes = size * 8ULL;
    if (bytes == 0) {
        return 0;
    }

    uint64_t k_off = PAGE_OFF(kernel_base_el1);
    uint64_t u_off = PAGE_OFF(user_base_el0);
    if (k_off != u_off) {
        // We require offsets to match so intra-page addresses line up 1:1.
        return -2;
    }

    // Compute page-aligned bases and number of 4 KiB pages to cover the range.
    uint64_t k_base_page = ((uint64_t)kernel_base_el1) & PAGE_MASK;
    uint64_t u_base_page =  user_base_el0              & PAGE_MASK;

    uint64_t span = k_off + bytes;                    // bytes from first page base to end
    uint64_t page_count = (span + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t i = 0; i < page_count; ++i) {
        const void *kva_page = (const void *)(k_base_page + i * PAGE_SIZE);
        uint64_t    uva_page =  u_base_page + i * PAGE_SIZE;

        // 1) Translate the kernel VA page base → PA page base.
        uint64_t pa_page;
        if (PageTable__el1_va_to_phys_pagebase(kva_page, &pa_page) != 0) {
            return -3; // source page not mapped at EL1
        }

        // 2) Install a 4 KiB leaf into the process tables for this page.
        int rc = PageTable__map_4k_page(process_l0_root,
                                          uva_page,
                                          pa_page,
                                          perms);
        if (rc != 0) {
            return -4;
        }
    }

    // 3) One TLB shootdown for the ASID (covers the whole updated range).
    __asm__ __volatile__("tlbi aside1, %0" :: "r"((uint64_t)asid_for_tlb) : "memory");
    __asm__ __volatile__("dsb ish; isb");

    return 0;
}

void PageTable__grant(Process * self) {
    // grant stack access
    PageTable * table = self->page_table;
    Stack stack = self->stack;
    size_t stack_size = stack.size;
    uint64_t stack_virt = 0xFFFFFFFF;

    void * entry = self->entry_point;
    // code wont relocate so we need it mapped to the same address
    uint64_t virt_code = (uint64_t) entry;

    MappingPermissions stack_perms = {
        .memory_type = ATTR_INDEX_NORMAL,
        .shareability = SHARE_INNER,
        .access = ACCESS_EL1_RW_EL0_RW,
        .uxn = 1,  // non-exec at EL0
        .pxn = 1   // non-exec at EL1
    };

    MappingPermissions execute_perms = {
        .memory_type = ATTR_INDEX_NORMAL,
        .shareability = SHARE_INNER,
        .access = ACCESS_EL1_RO_EL0_RO,
        .uxn = 0,  // non-exec at EL0
        .pxn = 0   // non-exec at EL1
    };

    PageTable__map_kernel_range_into_process(table, stack.base, stack_size, stack_virt, stack_perms, 1);
    PageTable__map_kernel_range_into_process(table, entry, 2048, (uint64_t) entry, execute_perms, 1);

}
