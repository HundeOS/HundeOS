// aarch64_el1_mmu.c
// Minimal EL1 stage-1 MMU enable for QEMU virt:
// - Identity maps kernel window [0x40000000..0x41000000) as Normal WBWA, executable
// - Identity maps PL011 at 0x09000000 (2MiB block) as Device-nGnRE, non-exec
// Build with -ffreestanding -nostdlib, compile as part of your kernel.

#include <stddef.h>
#include <stdint.h>
#include "../kernel/kernel.h"
#include "page_table.h"
#include "io/uart.h"
#include "asm_helpers.h"

// MAIR index meanings we set:
//  AttrIdx 0 = Normal, Inner/Outer WB WA, non-transient (0xFF)
//  AttrIdx 1 = Device-nGnRE (0x04)
#define MAIR_ATTR_NORMAL_WBWA  0xFFULL
#define MAIR_ATTR_DEVICE_nGnRE 0x04ULL
#define MAIR_VALUE  ((MAIR_ATTR_NORMAL_WBWA << 0) | (MAIR_ATTR_DEVICE_nGnRE << 8))

// TCR configuration: 48-bit VA space (T0SZ=16), 4KB TG0, 40-bit PA (IPS=0b010),
// Inner/Outer WBWA, Inner Shareable
#define TCR_T0SZ(x)     ((uint64_t)(x) & 0x3F)
#define TCR_TG0_4KB     (0ULL << 14)
#define TCR_SH0_IS      (3ULL << 12)
#define TCR_ORGN0_WBWA  (1ULL << 10)
#define TCR_IRGN0_WBWA  (1ULL << 8)
#define TCR_EPD0_EN     (0ULL << 7)
#define TCR_IPS_40BIT   (2ULL << 32)
#define TCR_VALUE ( \
    TCR_IPS_40BIT | TCR_TG0_4KB | TCR_SH0_IS | TCR_ORGN0_WBWA | TCR_IRGN0_WBWA | \
    TCR_EPD0_EN | TCR_T0SZ(16) \
)

// SCTLR bits
#define SCTLR_M     (1ULL << 0)
#define SCTLR_A     (1ULL << 1)  // (optional) alignment check
#define SCTLR_C     (1ULL << 2)
#define SCTLR_SA    (1ULL << 3)
#define SCTLR_I     (1ULL << 12)

#include <stdint.h>

#include "gicr.h"

extern char __kernel_start[];
extern char __text_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __heap_start[];
extern char __vectors_start[];

static PageTable * kernelPageTable = NULL;

// Helpers to write system registers

inline uint64_t align_down_2m(uint64_t x){ return x & ~((1ULL<<21)-1); }
inline uint64_t align_up_2m(uint64_t x){ return (x + ((1ULL<<21)-1)) & ~((1ULL<<21)-1); }


inline void MMU__map_kernel_all_normal_rwx(void)
{
    uint64_t k_start = (uint64_t)__kernel_start;      // 0x40080000
    uint64_t k_end   = (uint64_t)__heap_start;     // 0x485d8000
    uint64_t from    = align_down_2m(k_start);        // 0x40000000
    uint64_t to      = align_up_2m(k_end);            // 0x48600000

    const MappingPermissions perms = {
        .access = ACCESS_EL1_RW_EL0_NO,
        .shareability = SHARE_INNER,
        .memory_type = ATTR_INDEX_NORMAL,
        .uxn = 1,
        .pxn = 0
    };

    for (uint64_t va = from; va < to; va += (1ULL<<21)) {
        PageTable__map_2M_block(kernelPageTable, va, va, perms);
    }
}

inline void MMU__map_gicv3_mmio(void)
{
    const MappingPermissions perms = {
        .memory_type = ATTR_INDEX_DEVICE,
        .access = ACCESS_EL1_RW_EL0_NO,
        .shareability = SHARE_OUTER,
        .uxn = 1,
        .pxn = 1
    };

    PageTable__map_2M_block(kernelPageTable, GICD_BASE, GICD_BASE, perms);
    PageTable__map_2M_block(kernelPageTable, GICR_COVER, GICR_COVER, perms);
}

inline void MMU__map_uart() {
    PageTable__map_2M_block(kernelPageTable, UART0_BASE, UART0_BASE, (MappingPermissions){
        .memory_type = ATTR_INDEX_DEVICE,
        .access = ACCESS_EL1_RW_EL0_NO,
        .shareability = SHARE_OUTER,
        .uxn = 1,
        .pxn = 1
    });

}

inline void MMU__kernel_enable()
{
    __d_println(DEBUG, "setup mair");
    write_mair_el1(MAIR_VALUE);

    __d_println(DEBUG, "setup tcr");
    write_tcr_el1(TCR_VALUE);
    isb();

    __d_println(DEBUG, "setup el1 tbr0");
    write_ttbr0_el1((uint64_t)kernelPageTable);

    __d_println(DEBUG, "disabling interrupts");
    dsb_ish();
    isb();

    __d_println(DEBUG, "invalidating tlbs and enable mmu");
    tlbi_all();
    dsb_ish();
    isb();

    __d_println(DEBUG, "sctlr 1");

    uint64_t sctlr = read_sctlr_el1();
    sctlr |= (SCTLR_I | SCTLR_C | SCTLR_M);

    write_sctlr_el1(sctlr);

}

void MMU__init(void)
{
    __d_println(DEBUG, "allocate table");
    if (!kernelPageTable) {
        kernelPageTable = PageTable__new();
    }

    __d_println(DEBUG, "map uart");
    MMU__map_uart();

    __d_println(DEBUG, "map timers");
    MMU__map_gicv3_mmio();

    __d_println(DEBUG, "map kernel");
    MMU__map_kernel_all_normal_rwx();

    __d_println(DEBUG, "disable interrupts");
    dsb_ish();

    MMU__kernel_enable();
}

void MMU__secondary_init(void)
{
    MMU__kernel_enable();
}
