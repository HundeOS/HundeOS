#include <stdint.h>
#include <stddef.h>

#include "mmu.h"

#include "boot.h"
#include "boot_debug.h"
#include "../asm_helpers.h"
#include "../page_table.h"
#include "../mmu.h"

#include "print.h"
#include "boot_strings.h"
#include "exceptions.h"

extern const uint64_t __lma_start;
extern const uint64_t __lma_end;
extern const uint64_t __text_virt;
extern const uint64_t __text_phys;
#ifdef __BOOT_DEBUG
extern const uint64_t vector_table;
extern void CPU__enter_kernel(SystemInfoPtr system_info);
extern void memset(void * ptr, int value, size_t size);
#endif

extern const uint64_t __k_stack_high[];
extern const uint64_t __k_stack_start[];
extern const uint64_t __k_stack_phys_start[];
extern const uint64_t __k_stack_phys_end[];

extern const uint64_t __embedded_binaries_start[];
extern const uint64_t __embedded_binaries_end[];
extern const uint64_t __embedded_binaries_phys_start[];
extern const uint64_t __embedded_binaries_phys_end[];
extern const uint64_t __framebuffer_start[];

extern char __boot_end[];
static __BOOT_PAGE_TABLE__(PageTable) low_mem_l0_table;
static __BOOT_PAGE_TABLE__(PageTable) high_mem_l0_table;
static __BOOT_PAGE_TABLE__(PageTable) table_pool[PAGE_POOL_SIZE];
static __BOOT_DATA__(uint64_t) table_cursor = 0;

#define _2M (1ULL << 21)

extern char __table_phys_start[];
extern char __table_phys_end[];

extern char __table_virt_start[];
extern char __table_virt_end[];

static __BOOT_DATA__(MappingPermissions) stack_heap_perms = { // hundelint: ignore-constructor-required
    .access = ACCESS_EL1_RW_EL0_NO,
    .shareability = SHARE_INNER,
    .memory_type = ATTR_INDEX_NORMAL,
    .uxn = 1,
    .pxn = 1
};

static __BOOT_DATA__(MappingPermissions) kernel_perms = { // hundelint: ignore-constructor-required
    .access = ACCESS_EL1_RW_EL0_NO,
    .shareability = SHARE_INNER,
    .memory_type = ATTR_INDEX_NORMAL,
    .uxn = 1,
    .pxn = 0
};

static __BOOT_DATA__(MappingPermissions) kernel_rw_perms = { // hundelint: ignore-constructor-required
    .access = ACCESS_EL1_RW_EL0_NO,
    .shareability = SHARE_INNER,
    .memory_type = ATTR_INDEX_NORMAL,
    .uxn = 1,
    .pxn = 1
};

static __BOOT_DATA__(MappingPermissions) device_perms = { // hundelint: ignore-constructor-required
    .memory_type = ATTR_INDEX_DEVICE,
    .access = ACCESS_EL1_RW_EL0_NO,
    .shareability = SHARE_OUTER,
    .uxn = 1,
    .pxn = 1
};

static __BOOT_DATA__(MappingPermissions) embedded_binaries_perms = { // hundelint: ignore-constructor-required
    .memory_type = ATTR_INDEX_NORMAL,
    .access = ACCESS_EL1_RO_EL0_NO,
    .shareability = SHARE_INNER,
    .uxn = 1,
    .pxn = 1,
};


__BOOT__(PageTable *) ensure_next_level_table(PageTable * parent_level_table, uint64_t child_index)
{
    uint64_t descriptor = parent_level_table->entries[child_index];
    // Case 1: already a valid TABLE descriptor → return its next-level base.
    if ((descriptor & FLAG_PTE_VALID) && (descriptor & FLAG_PTE_TABLE))
    {
        return (PageTable *)(descriptor & MASK_PTE_NEXTADDR);
    }

    // Case 2: slot holds a valid LEAF (block/page) → caller must split it.
    // We do not overwrite it here to avoid silent semantic changes.
    if ((descriptor & FLAG_PTE_VALID) && !(descriptor & FLAG_PTE_TABLE))
    {
        return NULL;  // leaf present; cannot "ensure" a table without a split
    }

    PageTable * next_level_table = &table_pool[table_cursor++];

    for (int i = 0; i < TABLE_SIZE; i++)
    {
        next_level_table->entries[i] = 0;
    }
    if (table_cursor == PAGE_POOL_SIZE)
    {
        boot_panic(out_of_pages);
    }
    if (descriptor != 0)
    {
        boot_panic(bad_descriptor);
    }
    parent_level_table->entries[child_index] = PageTable__make_table_desc(next_level_table);
    dsb_ishst();
    return next_level_table;
}

__BOOT__(void) map_2M_block(PageTable * table, uint64_t virtual_address, uint64_t phys_addr, MappingPermissions * perms)
{
    PageTable * l1_tbl = ensure_next_level_table(table, L0_IDX(virtual_address));
    PageTable * l2_tbl = ensure_next_level_table(l1_tbl, L1_IDX(virtual_address));

    if (l2_tbl->entries[L2_IDX(virtual_address)] & FLAG_PTE_VALID)
    {
        boot_panic(block_mapped);
    }
    else {
        l2_tbl->entries[L2_IDX(virtual_address)] = PageTable__l2_block_desc(phys_addr,
                                                                            perms->memory_type,
                                                                            perms->shareability,
                                                                            perms->access,
                                                                            perms->uxn,
                                                                            perms->pxn);
    }
}

__BOOT__(void) map_range_2M(PageTable * table,
    uint64_t phys_start,
    uint64_t phys_end,
    uint64_t target,
    MappingPermissions * perms) {

    uint64_t from = align_down_2m(phys_start);
    uint64_t to = align_up_2m(phys_end);
    uint64_t dest = align_down_2m(target);
    uint64_t _2_megs = 1024 * 1024 * 2;

    for (uint64_t pa = from, va = dest; pa < to; pa += _2_megs, va += _2_megs)
    {
        map_2M_block(table, va, pa, perms);
    }
}


__BOOT__(void) map_kernel_heap_rw(SystemInfoPtr system_info)
{
    uint64_t target = system_info->kernel_heap.virtual_start;
    uint64_t phys_start = system_info->kernel_heap.physical_start;
    uint64_t phys_end = system_info->kernel_heap.physical_end;

    map_range_2M(&high_mem_l0_table, phys_start, phys_end, target, &stack_heap_perms);
}

__BOOT__(void) map_kernel_stack_rw()
{
    uint64_t target = (uint64_t)&__k_stack_start;
    uint64_t phys_start = (uint64_t)&__k_stack_phys_start;
    uint64_t phys_end = (uint64_t)&__k_stack_phys_end;

    map_range_2M(&high_mem_l0_table, phys_start, phys_end, target, &stack_heap_perms);
}

__BOOT__(void) map_embedded_binaries_ro()
{
    uint64_t target = (uint64_t)&__embedded_binaries_start;
    uint64_t phys_start = (uint64_t)&__embedded_binaries_phys_start;
    uint64_t phys_end = (uint64_t)&__embedded_binaries_phys_end;

    boot_debug_print_unsigned(target);
    boot_debug_print(new_line);
    boot_debug_print_unsigned(phys_start);
    boot_debug_print(new_line);
    boot_debug_print_unsigned(phys_end);
    boot_debug_print(new_line);
    map_range_2M(&high_mem_l0_table, phys_start, phys_end, target, &embedded_binaries_perms);
}

__BOOT__(void) map_kernel_all_normal_rwx(uint64_t phys_start,
                                         uint64_t phys_end,
                                         uint64_t target,
                                         PageTable * table)
{
    map_range_2M(table, phys_start, phys_end, target, &kernel_perms);
}

__BOOT__(void) map_uart(PageTable * table, uint64_t virt_addr, uint64_t phys_addr)
{
    map_2M_block(table, virt_addr, phys_addr, &device_perms);
}

__BOOT__(void) map_gicd_gicr(PageTable * table, BootPlatformInfoPtr platform)
{
    map_2M_block(table,
        platform->gic_distributor_virtual_base,
        platform->gic_distributor_physical_base,
        &device_perms);
}

__BOOT__(void) map_framebuffer(PageTable * table, BootPlatformInfoPtr platform)
{
    match_optional(FramebufferInfo, platform->framebuffer,
        some(framebuffer)
        {
            uint64_t map_size = framebuffer.size;
            if (map_size > BOOT_FRAMEBUFFER_MAP_SIZE)
            {
                map_size = BOOT_FRAMEBUFFER_MAP_SIZE;
            }
            map_range_2M(
                table,
                framebuffer.physical_base,
                framebuffer.physical_base + map_size,
                (uint64_t)&__framebuffer_start[0],
                &device_perms
            );
        },
        none_do_nothing()
    );
}

__BOOT__(void) map_kernel(SystemInfoPtr system_info)
{
    uint64_t k_low_phys = KERNEL_LOW_PHYS_BASE;
    uint64_t k_low_phys_max = (uintptr_t)&__boot_end[0];
    uint64_t k_low_virt_base = KERNEL_LOW_VIRT_BASE;

    map_kernel_all_normal_rwx(
        k_low_phys,
        k_low_phys_max,
        k_low_virt_base,
        &low_mem_l0_table); // unity map

    uint64_t k_high_phys = (uintptr_t)&__text_phys;
    uint64_t k_high_phys_max = (uintptr_t)&__lma_end;
    uint64_t k_high_virt_base = (uintptr_t)&__text_virt;

    map_kernel_all_normal_rwx(
        k_high_phys,
        k_high_phys_max,
        k_high_virt_base,
        &high_mem_l0_table);

    map_kernel_heap_rw(system_info);
}

__BOOT__(void) map_tables_high()
{
    map_range_2M(&high_mem_l0_table,
        (uint64_t)&__table_phys_start,
        (uint64_t)&__table_phys_end,
        (uint64_t)&__table_virt_start,
        &kernel_rw_perms);
}

#ifdef __BOOT_DEBUG
static __BOOT__(void) debug_boot_mapping(const char * label, uint64_t va)
{
    uint64_t pa = 0;
    uint64_t desc = 0;
    int level = 0;
    const uint64_t * root = pick_l0_for_va(va, (const uint64_t *)&low_mem_l0_table, (const uint64_t *)&high_mem_l0_table);
    int ok = va_to_pa_from_l0(root, va, &pa, &level, &desc);

    boot_debug_print(mapping_check);
    boot_debug_print(label);
    boot_debug_print(hex_pre);
    boot_debug_print_unsigned(va);

    if (ok)
    {
        boot_debug_print(mapping_ok);
        boot_debug_print(mapping_pa);
        boot_debug_print_unsigned(pa);
        boot_debug_print(mapping_level);
        boot_debug_print_base((uint64_t)level, 10);
        boot_debug_print(mapping_desc);
        boot_debug_print_unsigned(desc);
    }
    else
    {
        boot_debug_print(mapping_missing);
    }
    boot_debug_print(new_line);
}

__BOOT__(void) debug_boot_mappings(SystemInfoPtr system_info)
{
    debug_boot_mapping(mapping_cpu_enter, (uint64_t)&CPU__enter_kernel);
    debug_boot_mapping(mapping_memset, (uint64_t)&memset);
    debug_boot_mapping(mapping_vectors, (uint64_t)&vector_table);
    debug_boot_mapping(mapping_stack, system_info->kernel_stack.virtual_start);
}
#endif

__BOOT__(void) zero_table(PageTable * table)
{
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        table->entries[i] = 0;
    }
}

__BOOT__(void) zero_l0_tables()
{
    boot_debug_print(message_zero);
    zero_table(&low_mem_l0_table);
    zero_table(&high_mem_l0_table);
}

__BOOT__(void) zero_table_pool()
{
    boot_debug_print(message_zero);

    for (int i = 0; i < PAGE_POOL_SIZE; i++)
    {
        zero_table(&table_pool[i]);
    }
}

__BOOT__(void) enable_mmu()
{
    boot_debug_print(enabling_mmu);
    write_mair_el1(MAIR_VALUE);

    write_tcr_el1(boot_tcr_el1());
    isb();
    write_ttbr1_el1((uint64_t)&high_mem_l0_table);
    dsb_ish(); isb();
    tlbi_vmalle1();           // flush stale translations
    dsb_ish(); isb();
    write_ttbr0_el1((uint64_t)&low_mem_l0_table);
    dsb_ish();
    isb();

    tlbi_all();
    dsb_ish();
    isb();

    uint64_t sctlr = read_sctlr_el1();

    // ensure all RES1 bits are set
    sctlr |= SCTLR_RES1;

    // turn on MMU + caches
    sctlr |= (SCTLR_M | SCTLR_C | SCTLR_I);
    sctlr |= (SCTLR_NTWI | SCTLR_NTWE);
    sctlr |= (SCTLR_UCT | SCTLR_UCI);

    // early bring-up: keep alignment checks off unless you really want them
    sctlr &= ~(SCTLR_A | SCTLR_SA | SCTLR_SA0);

    // optional: keep WXN=0 for now; set later when your X permissions are correct
    sctlr &= ~SCTLR_WXN;

    write_sctlr_el1(sctlr);

    write_ttbr1_el1((uint64_t)&high_mem_l0_table);
    dsb_ish(); isb();
    tlbi_vmalle1();           // flush stale translations
    dsb_ish(); isb();
    write_ttbr0_el1((uint64_t)&low_mem_l0_table);
    dsb_ish();
    isb();
    boot_debug_print(mmu_enabled);
}

__BOOT__(void) setup_boot_mappings(BootPlatformInfoPtr platform, SystemInfoPtr system_info)
{
    zero_l0_tables();
    zero_table_pool();
    boot_debug_print(mapping_kernel);
    map_kernel(system_info);
    map_tables_high();
    map_embedded_binaries_ro();
    map_kernel_stack_rw();
    boot_debug_print(mapping_uart_low);
    map_uart(&low_mem_l0_table, platform->uart_physical_base, platform->uart_physical_base);
    boot_debug_print(mapping_uart_high);
    map_uart(&high_mem_l0_table, platform->uart_virtual_base, platform->uart_physical_base);

    boot_debug_print(mapping_gicd);
    map_gicd_gicr(&high_mem_l0_table, platform);
    map_framebuffer(&high_mem_l0_table, platform);
    boot_debug_print(mapping_kernel_complete);
#ifdef __BOOT_DEBUG
    debug_boot_mappings(system_info);
#endif
}
