// aarch64_el1_mmu.c
// Minimal EL1 stage-1 MMU enable for QEMU virt:
// - Identity maps kernel window [0x40000000..0x41000000) as Normal WBWA, executable
// - Identity maps PL011 at 0x09000000 (2MiB block) as Device-nGnRE, non-exec
// Build with -ffreestanding -nostdlib, compile as part of your kernel.

#include <stdint.h>
#include "../kernel/kernel.h"
#include "page_table.h"

#define PAGE_SIZE        0x1000UL
#define Lx_ENTRIES       512

// Descriptor bits (AArch64, 4KB granule)
#define DESC_VALID       (1ULL << 0)
#define DESC_TABLE       (1ULL << 1)   // when set with VALID => table; when clear with VALID at L1/L2 => block
#define PTE_AF           (1ULL << 10)
#define PTE_SH_IS        (3ULL << 8)   // Inner Shareable
#define PTE_SH_OS        (2ULL << 8)   // Outer Shareable
#define PTE_AP_RW_EL1    (0ULL << 6)   // EL1 RW, EL0 no access
#define PTE_ATTRINDX(n)  ((uint64_t)(n) << 2)
#define PTE_UXN          (1ULL << 54)
#define PTE_PXN          (1ULL << 53)
#define PTE_SH_SHIFT   8               // Shareability

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

// #define MAIR_VALUE   (0xFFull << 0 /*Attr0 Normal WBWA*/ | 0x04ull << 8 /*Attr1 Device-nGnRE*/)
// #define TCR_VALUE    ((2ull<<32) /*IPS=40b*/ | (0ull<<14) /*TG0=4K*/ | (3ull<<12) /*SH0 IS*/ | (1ull<<10) /*ORGN0 WBWA*/ | (1ull<<8) /*IRGN0 WBWA*/ | (16ull /*T0SZ*/))

#define GICD_BASE   0x08000000ULL   // Distributor
#define GICR_COVER  0x08080000ULL   // 2 MiB block covering Redistributor frames (e.g. 0x080A0000)


extern char __kernel_start[];
extern char __text_start[];
extern char  __rodata_end[];
extern char __data_start[];
extern char __page_pool_end[];
extern char __vectors_start[];

uint64_t __attribute__((aligned(PAGE_SIZE))) L0[Lx_ENTRIES];
uint64_t __attribute__((aligned(PAGE_SIZE))) L1_0[Lx_ENTRIES]; // covers VA 0..512GB chunk #0
uint64_t __attribute__((aligned(PAGE_SIZE))) L2_0[Lx_ENTRIES]; // for VA 0..1GB
uint64_t __attribute__((aligned(PAGE_SIZE))) L2_1[Lx_ENTRIES]; // for VA 1..2GB (0x40000000..0x7FFFFFFF)

// Helpers to write system registers
static inline void write_sctlr_el1(uint64_t v){ __asm__ volatile("msr sctlr_el1, %0" :: "r"(v)); }
static inline void write_tcr_el1(uint64_t v){ __asm__ volatile("msr tcr_el1, %0" :: "r"(v)); }
static inline void write_ttbr0_el1(uint64_t v){ __asm__ volatile("msr ttbr0_el1, %0" :: "r"(v)); }
static inline void write_mair_el1(uint64_t v){ __asm__ volatile("msr mair_el1, %0" :: "r"(v)); }
static inline uint64_t read_sctlr_el1(void){ uint64_t v; __asm__ volatile("mrs %0, sctlr_el1":"=r"(v)); return v; }
static inline uint64_t rd_currentel(void){ uint64_t v; __asm__ volatile("mrs %0,currentel":"=r"(v)); return v; }
static inline uint64_t rd_spsel(void){ uint64_t v; __asm__ volatile("mrs %0,spsel":"=r"(v)); return v; }
static inline uint64_t rd_mair(void){ uint64_t v; __asm__ volatile("mrs %0,mair_el1":"=r"(v)); return v; }
static inline uint64_t rd_tcr(void){ uint64_t v; __asm__ volatile("mrs %0,tcr_el1":"=r"(v)); return v; }
static inline uint64_t rd_ttbr0(void){ uint64_t v; __asm__ volatile("mrs %0,ttbr0_el1":"=r"(v)); return v; }
static inline void mask_daif(void){ __asm__ volatile("msr daifset,#0xf"); }
static inline void unmask_daif(void){ __asm__ volatile("msr daifclr,#0xf"); }

static inline void isb(void){ __asm__ volatile("isb"); }
static inline void dsb_ish(void){ __asm__ volatile("dsb ish"); }
static inline void tlbi_all(void){ __asm__ volatile("tlbi vmalle1"); }

inline uint64_t align_down_2m(uint64_t x){ return x & ~((1ULL<<21)-1); }
inline uint64_t align_up_2m(uint64_t x){ return (x + ((1ULL<<21)-1)) & ~((1ULL<<21)-1); }


// Create a level-2 block descriptor (2MiB) with given PA base, mem type, shareability, exec perms.
inline uint64_t l2_block_desc(uint64_t pa, MairAttribute attridx, AccessAttribute access, ShareAttribute sh, int uxn, int pxn)
{
    uint64_t desc = 0;
    // For L2 block: VALID=1, TABLE=0
    desc |= DESC_VALID;               // block when bit1=0 at L1/L2
    desc |= PTE_AF;                   // access flag
    desc |= sh;
    desc |= access;
    desc |= PTE_ATTRINDX(attridx);
    // Execute-never flags if not executable
    if (uxn) desc |= PTE_UXN;
    if (pxn) desc |= PTE_PXN;
    // PA[47:21] go into [47:21] of descriptor (block aligned at 2MiB)
    desc |= (pa & 0xFFFFFFFFFFE00000ULL);
    return desc;
}



static void map_kernel_all_normal_rwx(void)
{
    uint64_t k_start = (uint64_t)__kernel_start;      // 0x40080000
    uint64_t k_end   = (uint64_t)__page_pool_end;     // 0x485d8000
    uint64_t from    = align_down_2m(k_start);        // 0x40000000
    uint64_t to      = align_up_2m(k_end);            // 0x48600000

    for (uint64_t va = from; va < to; va += (1ULL<<21)) {
        uint64_t i = (va >> 21) & 0x1FF;              // L2 index within the [1..2GiB] slice
        L2_1[i] = l2_block_desc(
            va,
            ATTR_INDEX_NORMAL,       // MAIR slot 0 = Normal WBWA
            ACCESS_EL1_RW_EL0_NO,
            SHARE_INNER,
            1,
            0
        );
    }
}

static int pt_probe_el1_va_mapped(uint64_t *l0_root, uint64_t va, uint64_t *out_desc)
{
    // Common helpers
    const uint64_t VALID = FLAG_PTE_VALID;
    const uint64_t TABLE = FLAG_PTE_TABLE;
    const uint64_t NEXT  = MASK_PTE_NEXTADDR;

    // L0
    uint64_t d = l0_root[(va >> 39) & 0x1FF];
    if ((d & VALID) == 0) return 0;
    if ((d & TABLE) == 0) {                 // L0 block (not used with 4KiB TG), treat as unmapped
        if (out_desc) *out_desc = d;
        return 0;
    }
    uint64_t *l1 = (uint64_t *)(d & NEXT);

    // L1
    d = l1[(va >> 30) & 0x1FF];
    if ((d & VALID) == 0) return 0;
    if ((d & TABLE) == 0) {                 // L1 block (1 GiB)
        if (out_desc) *out_desc = d;
        return 1;
    }
    uint64_t *l2 = (uint64_t *)(d & NEXT);

    // L2
    d = l2[(va >> 21) & 0x1FF];
    if ((d & VALID) == 0) return 0;
    if ((d & TABLE) == 0) {                 // L2 block (2 MiB)
        if (out_desc) *out_desc = d;
        return 1;
    }
    uint64_t *l3 = (uint64_t *)(d & NEXT);

    // L3 (page)
    d = l3[(va >> 12) & 0x1FF];
    if ((d & VALID) == 0) return 0;         // unmapped page
    // At L3, a valid leaf must be a "page" (bit1 == 1).
    if ((d & TABLE) == 0) return 0;         // defensive: unexpected non-page at L3
    if (out_desc) *out_desc = d;
    return 1;
}

static void mmu_preflight_strict() {
    // 1) EL should be EL1h: CurrentEL==0x4 (EL1), SPsel==0 (SP_EL0) or 1 (SP_ELx) is OK
    uint64_t cel = rd_currentel();   // expect 0x4xx (EL1)
    uint64_t sps = rd_spsel();
    k_printf("EL=0x%x SPSel=0x%x\n", (unsigned long long)cel, (unsigned long long)sps);

    // 2) Table base PA aligned
    uint64_t ttbr0_pa = (uint64_t)(uintptr_t)L0;
    if (ttbr0_pa & 0xFFF) panic("TTBR0 not 4K aligned");

    // 3) You *must* program MAIR/TCR to known-good constants BEFORE enabling M
    k_printf("pre MAIR=0x%x TCR=0x%x TTBR0=0x%x\n",
             (unsigned long long)rd_mair(),
             (unsigned long long)rd_tcr(),
             (unsigned long long)rd_ttbr0());

    // 4) Software walk a couple of VAs you’ll execute from
    uint64_t d;
    uint64_t pc = (uint64_t)(uintptr_t)mmu_preflight_strict;
    if (!pt_probe_el1_va_mapped(L0, pc, &d)) panic("PC VA not mapped");
    __d_printf(DEBUG, "PC mapping descriptor: 0x%x\n", d);
    if (d & (1ull<<53)) panic("PC XN");

    if (!pt_probe_el1_va_mapped(L0, (uint64_t)(uintptr_t)__vectors_start, &d)) panic("VBAR VA not mapped");
    if (d & (1ull<<53)) panic("VBAR XN");
}

// ----- Flags/masks you already use -----
#define FLAG_PTE_VALID  (1ull<<0)
#define FLAG_PTE_TABLE  (1ull<<1)
#define FLAG_PTE_PXN    (1ull<<53)
#define FLAG_PTE_UXN    (1ull<<54)
#define NEXTADDR_MASK   0x0000FFFFFFFFF000ULL
#define L2_BLOCK_MASK   0x0000FFFFFFFFE00000ULL
#define L1_BLOCK_MASK   0x0000FFFFC0000000ULL

static void dump_mapping(uint64_t *l0_root, const char *name, uint64_t va)
{
    uint64_t d;
    int ok = pt_probe_el1_va_mapped(l0_root, va, &d);
    k_printf("%s VA=0x%x -> %s, desc=0x%x\n",
             name, (unsigned long long)va,
             ok ? "MAPPED" : "UNMAPPED",
             (unsigned long long)(ok?d:0ULL));
    if (!ok) return;

    unsigned attr = (unsigned)((d >> 2) & 7);
    unsigned sh   = (unsigned)((d >> 8) & 3);
    unsigned ap   = (unsigned)((d >> 6) & 3);
    int pxn = !!(d & FLAG_PTE_PXN);
    int uxn = !!(d & FLAG_PTE_UXN);
    int is_table = !!(d & FLAG_PTE_TABLE);

    k_printf("  table=%d attridx=0x%x sh=0x%x ap=0x%x pxn=0x%x uxn=0x%x\n",
             is_table, attr, sh, ap, pxn, uxn);

    // Compute the output PA the leaf points to (rough but useful):
    uint64_t pa;
    if (!is_table) {
        // block: decide size by mask shape
        if ((d & L1_BLOCK_MASK) == (d & ~((1ULL<<30)-1))) {
            pa = d & L1_BLOCK_MASK;
        } else {
            pa = d & L2_BLOCK_MASK;
        }
    } else {
        pa = d & NEXTADDR_MASK; // L3 page
    }
    k_printf("  output PA (base of leaf) = 0x%x\n", (unsigned long long)pa);
}

static void mmu_debug_walk_points(void)
{
    dump_mapping(L0, "PC",   (uint64_t)(uintptr_t)&mmu_debug_walk_points);
    dump_mapping(L0, "VBAR", (uint64_t)(uintptr_t)__vectors_start);
}

#define L2_INDEX(va)   (((va) >> 21) & 0x1FF)

inline void map_gicv3_mmio(void)
{
    // Device, Outer-shareable, EL1 RW, EL0 NO, NX both
    const MairAttribute attridx = ATTR_INDEX_DEVICE;
    const AccessAttribute ap = ACCESS_EL1_RW_EL0_NO;
    const ShareAttribute sh = SHARE_OUTER;

    L2_0[L2_INDEX(GICD_BASE)]  = l2_block_desc(GICD_BASE,  attridx, ap, sh, 1, 1);
    L2_0[L2_INDEX(GICR_COVER)] = l2_block_desc(GICR_COVER, attridx, ap, sh, 1, 1);
}

inline void map_uart() {
    // ---- Map PL011 UART region (2MiB block covering 0x09000000) as Device, non-exec ----
    // 2MiB-aligned base for block containing 0x09000000:
    const uint64_t uart_pa   = 0x09000000ULL;
    const uint64_t uart_blk  = uart_pa & ~((1ULL<<21)-1); // 2MiB align -> 0x08800000
    // Index within L2_0 for VA 0..1GB: bits [29:21]
    const uint64_t uart_idx  = (uart_blk >> 21) & 0x1FF;
    __d_println(DEBUG, "map uart");
    L2_0[uart_idx] = l2_block_desc(uart_blk, ATTR_INDEX_DEVICE, ACCESS_EL1_RW_EL0_NO, SHARE_OUTER, 1, 1);

}

void mmu_init(void)
{
    __d_printf(DEBUG, "__text_start=0x%x, __rodata_end=0x%x, __data_start=0x%x, __page_pool_end=0x%x\n ",
        __text_start, __rodata_end, __data_start, __page_pool_end)
    __d_println(DEBUG, "zero tables");
    // Zero tables
    for (int i=0;i<Lx_ENTRIES;i++){ L0[i]=0; L1_0[i]=0; L2_0[i]=0; L2_1[i]=0; }

    __d_println(DEBUG, "zero tables");
    // L0[0] -> L1_0
    L0[0] = ((uint64_t)L1_0) | DESC_VALID | DESC_TABLE;

    // L1_0[0] -> L2_0 (VA 0..1GB)
    L1_0[0] = ((uint64_t)L2_0) | DESC_VALID | DESC_TABLE;

    // L1_0[1] -> L2_1 (VA 1GB..2GB)
    L1_0[1] = ((uint64_t)L2_1) | DESC_VALID | DESC_TABLE;

    __d_println(DEBUG, "map uart");
    map_uart();

    __d_println(DEBUG, "map timers");
    map_gicv3_mmio();

    __d_println(DEBUG, "map kernel");
    map_kernel_all_normal_rwx();

    __d_println(DEBUG, "disable interrupts");
    dsb_ish();

    __d_println(DEBUG, "setup mair");
    write_mair_el1(MAIR_VALUE);

    __d_println(DEBUG, "setup tcr");
    write_tcr_el1(TCR_VALUE);
    isb();

    __d_println(DEBUG, "setup el1 tbr0");
    write_ttbr0_el1((uint64_t)L0);

    __d_println(DEBUG, "disabling interrupts");
    dsb_ish();
    isb();

    __d_println(DEBUG, "invalidating tlbs and enable mmu");
    tlbi_all();
    dsb_ish();
    isb();

    // mmu_preflight_strict();
    // mmu_debug_walk_points();
    __d_println(DEBUG, "sctlr 1");

    uint64_t sctlr = read_sctlr_el1();
    sctlr |= (SCTLR_I | SCTLR_C | SCTLR_M);

    write_sctlr_el1(sctlr);
    isb();
}


void mmu_secondary_init(void)
{
    dsb_ish();
    write_mair_el1(MAIR_VALUE);
    write_tcr_el1(TCR_VALUE);
    isb();

    write_ttbr0_el1((uint64_t)L0);
    dsb_ish(); isb();

    tlbi_all(); dsb_ish(); isb();

    uint64_t sctlr = read_sctlr_el1();
    sctlr |= (SCTLR_I|SCTLR_C|SCTLR_M);
    write_sctlr_el1(sctlr);
    isb();
}
