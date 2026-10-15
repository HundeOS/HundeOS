#include "boot.h"
#include "boot_debug.h"
#include "print.h"
#include "boot_strings.h"

__BOOT__(uint64_t) stack_pointer()
{
    uint64_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

__BOOT__(uint64_t) program_counter()
{
    uint64_t pc = (uint64_t)&&pc;
pc:
    return pc;
}

__BOOT__(PageTablePtr) table_at(PageTablePtr table, unsigned index)
{
    uint64_t descriptor = table->entries[index];
    return (PageTablePtr)(descriptor & PTE_NEXTADDR_MASK);
}

__BOOT__(uint64_t) pa_bits_from_tcr_ips(uint64_t ips)
{
    // ARM ARM: IPS encodings
    switch (ips & 7u)
    {
    case 0:
        return 32;
    case 1:
        return 36;
    case 2:
        return 40;
    case 3:
        return 42;
    case 4:
        return 44;
    case 5:
        return 48;
    default:
        return 40; // sane fallback
    }
}

__BOOT__(uint64_t) read_pa_bits()
{
    uint64_t tcr;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
    unsigned ips = (unsigned)((tcr >> 32) & 7ull);
    return pa_bits_from_tcr_ips(ips);
}

__BOOT__(uint64_t) clamp_pa_to_width(uint64_t pa)
{
    unsigned pb = read_pa_bits(); // e.g. 40
    if (pb >= 64)
        return pa;
    uint64_t mask = (pb == 64) ? ~0ull : ((1ull << pb) - 1ull);
    return pa & mask;
}

__BOOT__(int)

va_to_pa_from_l0(const uint64_t * l0_root, uint64_t va, uint64_t * pa_out,
                 int * leaf_level_out,     // 1,2,3 (optional)
                 uint64_t * leaf_desc_out) // raw descriptor (optional)
{
    // Indices for 4 KiB granule
    const uint64_t i0 = (va >> 39) & 0x1FF;
    const uint64_t i1 = (va >> 30) & 0x1FF;
    const uint64_t i2 = (va >> 21) & 0x1FF;
    const uint64_t i3 = (va >> 12) & 0x1FF;

    // L0
    uint64_t d0 = l0_root[i0];
    if (!(d0 & DESC_VALID) || !(d0 & DESC_TABLE))
        return 0;
    const uint64_t * l1 = (const uint64_t *)(d0 & MASK_NEXTADDR);

    // L1
    uint64_t d1 = l1[i1];
    if (!(d1 & DESC_VALID))
        return 0;

    if (!(d1 & DESC_TABLE))
    {
        // L1 BLOCK (1 GiB)
        uint64_t pa = (d1 & MASK_L1_BLOCK_PA) | (va & ((1ull << 30) - 1));
        if (pa_out)
            *pa_out = pa;
        if (leaf_level_out)
            *leaf_level_out = 1;
        if (leaf_desc_out)
            *leaf_desc_out = d1;
        return 1;
    }

    const uint64_t * l2 = (const uint64_t *)(d1 & MASK_NEXTADDR);

    // L2
    uint64_t d2 = l2[i2];
    if (!(d2 & DESC_VALID))
        return 0;

    if (!(d2 & DESC_TABLE))
    {
        // L2 BLOCK (2 MiB)
        uint64_t pa = (d2 & MASK_L2_BLOCK_PA) | (va & ((1ull << 21) - 1));
        if (pa_out)
            *pa_out = pa;
        if (leaf_level_out)
            *leaf_level_out = 2;
        if (leaf_desc_out)
            *leaf_desc_out = d2;
        return 1;
    }

    const uint64_t * l3 = (const uint64_t *)(d2 & MASK_NEXTADDR);

    // L3 (4 KiB page)
    uint64_t d3 = l3[i3];
    if (!(d3 & DESC_VALID))
        return 0;

    // Page: [47:12] + VA[11:0]
    uint64_t pa = (d3 & MASK_NEXTADDR) | (va & 0xFFFull);
    if (pa_out)
        *pa_out = pa;
    if (leaf_level_out)
        *leaf_level_out = 3;
    if (leaf_desc_out)
        *leaf_desc_out = d3;
    return 1;
}

// Convenience: choose root by address band (48-bit canonical VA with T0SZ=T1SZ=16)
__BOOT__(const uint64_t *) pick_l0_for_va(uint64_t va, const uint64_t * ttbr0_l0, const uint64_t * ttbr1_l0)
{
    // High canonical addresses have VA[63:48] == 0xFFFF
    return (va >> 63) ? ttbr1_l0 : ttbr0_l0;
}

// Example helper that prints/compares a symbol:
__BOOT__(uint64_t) check_symbol_va_to_pa(uint64_t va, const uint64_t * low_l0, const uint64_t * high_l0)
{
    const uint64_t * root = pick_l0_for_va(va, low_l0, high_l0);
    uint64_t pa = 0, desc = 0;
    int level = 0;
    int ok = va_to_pa_from_l0(root, va, &pa, &level, &desc);
    if (!ok)
    {
        return -1;
    }
    pa = clamp_pa_to_width(pa);

    return pa;
    ;
}

__BOOT__(uint64_t) va_to_pa_hw(uint64_t va)
{
    uint64_t par = 0;
    __asm__ volatile("at    s1e1r, %1\n"   // Address Translate (read) at EL1 stage-1
                     "isb\n"               // Ensure PAR_EL1 is architecturally updated
                     "mrs   %0, par_el1\n" // Read Physical Address Register
                     : "=r"(par)           // %0: output
                     : "r"(va)             // %1: input  (the VA to translate)
                     : "memory");
    if (par & 1)
        return -1; // F==1 => translation fault
    // For success: OA is in PAR_EL1[51:12], low 12 bits are zero
    return (par & 0x0000FFFFFFFFF000ULL) | (va & 0xFFF);
}

__BOOT__(void) __debug_table(PageTablePtr table, uint64_t addr, const char * message, int entries)
{
    PageTablePtr l1_table = table_at(table, idx0(addr));
    PageTablePtr l2_table = table_at(l1_table, idx1(addr));

    boot_debug_print(start_end_dump);
    boot_debug_print(message);
    boot_debug_print(hex_pre);
    boot_debug_print_unsigned(addr);
    boot_debug_print(new_line);
    boot_debug_print_base(idx0(addr), 10);
    boot_debug_print(new_line);
    boot_debug_print_base(idx1(addr), 10);
    boot_debug_print(new_line);
    boot_debug_print_base(idx2(addr), 10);
    boot_debug_print(new_line);

    boot_debug_print_unsigned(addr & PTE_OFFSET_MASK);
    boot_debug_print(new_line);

    uint64_t descriptor = l2_table->entries[idx2(addr)];
    boot_debug_print(desc_eq);
    boot_debug_print_binary(descriptor);
    // _print(sep);
    // _print_unsigned((uint64_t)l2_table);

    __dump_table(table, entries, L0);
    __dump_table(l1_table, entries, L1);
    __dump_table(l2_table, entries, L2);
    boot_debug_print(start_end_dump);
}

__BOOT__(void) __dump_table(PageTablePtr table, int entries, const char * level)
{
    boot_debug_print(new_line);
    boot_debug_print(level);
    boot_debug_print(addr_spacer);
    boot_debug_print_unsigned((uint64_t)table);
    boot_debug_print(new_line);

    int non_zero = 0;
    for (size_t i = 0; i < TABLE_SIZE && non_zero < entries; i++)
    {
        PageTableEntry entry = table->entries[i];
        if (entry)
        {
            non_zero++;

            boot_debug_print_base(i, 10);
            boot_debug_print(spacer);
            boot_debug_print_binary(table->entries[i]);
            boot_debug_print(addr_spacer);
            boot_debug_print_unsigned(entry & PTE_NEXTADDR_MASK);
            boot_debug_print(new_line);
        }
    }
}

__BOOT__(void)

debug_table_pool(int entries, PageTablePtr low_mem_l0_table, PageTablePtr high_mem_l0_table, PageTable table_pool[],
                 int cursor)
{
    boot_debug_print(new_line);
    boot_debug_print(root_table);
    boot_debug_print_base(0, 10);
    boot_debug_print(new_line);
    __dump_table(low_mem_l0_table, entries, pool_level);

    boot_debug_print(new_line);
    boot_debug_print(root_table);
    boot_debug_print_base(1, 10);
    boot_debug_print(new_line);
    __dump_table(high_mem_l0_table, entries, pool_level);

    for (int i = 0; i < cursor; i++)
    {
        boot_debug_print(pool_spacer);
        boot_debug_print_base(i, 10);
        boot_debug_print(new_line);

        __dump_table(&table_pool[i], entries, pool_level);
    }
}
