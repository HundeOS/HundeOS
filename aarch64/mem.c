// aarch64_el1_mmu.c
// Minimal EL1 stage-1 MMU enable for QEMU virt:
// - Identity maps kernel window [0x40000000..0x41000000) as Normal WBWA, executable
// - Identity maps PL011 at 0x09000000 (2MiB block) as Device-nGnRE, non-exec
// Build with -ffreestanding -nostdlib, compile as part of your kernel.

#include <stdint.h>
#include "../kernel/kernel.h"

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

static inline void isb(void){ __asm__ volatile("isb"); }
static inline void dsb_ish(void){ __asm__ volatile("dsb ish"); }
static inline void tlbi_all(void){ __asm__ volatile("tlbi vmalle1"); }

// Create a level-2 block descriptor (2MiB) with given PA base, mem type, shareability, exec perms.
static inline uint64_t l2_block_desc(uint64_t pa, int attridx, uint64_t sh, int executable)
{
    uint64_t desc = 0;
    // For L2 block: VALID=1, TABLE=0
    desc |= DESC_VALID;               // block when bit1=0 at L1/L2
    desc |= PTE_AF;                   // access flag
    desc |= PTE_AP_RW_EL1;
    desc |= sh;
    desc |= PTE_ATTRINDX(attridx);
    // Execute-never flags if not executable
    if (!executable) desc |= PTE_UXN | PTE_PXN;
    // PA[47:21] go into [47:21] of descriptor (block aligned at 2MiB)
    desc |= (pa & 0xFFFFFFFFFFE00000ULL);
    return desc;
}

void mmu_init(void)
{
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

    // ---- Map PL011 UART region (2MiB block covering 0x09000000) as Device, non-exec ----
    // 2MiB-aligned base for block containing 0x09000000:
    const uint64_t uart_pa   = 0x09000000ULL;
    const uint64_t uart_blk  = uart_pa & ~((1ULL<<21)-1); // 2MiB align -> 0x08800000
    // Index within L2_0 for VA 0..1GB: bits [29:21]
    const uint64_t uart_idx  = (uart_blk >> 21) & 0x1FF;
    __d_println(DEBUG, "map uart");
    L2_0[uart_idx] = l2_block_desc(uart_blk, /*attridx=*/1, /*share=*/PTE_SH_OS, /*exec=*/0);

    // ---- Map kernel window [0x40000000..0x41000000) as Normal WBWA, executable ----
    // This is 16MiB -> 8 blocks of 2MiB starting at 0x40000000.
    __d_println(DEBUG, "map kernel");
    uint64_t k_base = 0x40000000ULL;
    for (int b = 0; b < 8; b++) {
        uint64_t blk = k_base + ((uint64_t)b << 21);
        // L2_1 corresponds to VA 1..2GB, so index uses bits [29:21] of VA
        uint64_t idx = (blk >> 21) & 0x1FF;
        L2_1[idx] = l2_block_desc(blk, /*attridx=*/0, /*share=*/PTE_SH_IS, /*exec=*/1);
    }

    __d_println(DEBUG, "disable interrupts");
    dsb_ish();

    // MAIR: AttrIdx0 = Normal WBWA (0xFF), AttrIdx1 = Device-nGnRE (0x04)
    __d_println(DEBUG, "setup mair");
    write_mair_el1(MAIR_VALUE);

    // TCR: 48-bit VA space on TTBR0, 4KB granule, WBWA, Inner Shareable, 40-bit PA
    __d_println(DEBUG, "setup tcr");
    write_tcr_el1(TCR_VALUE);
    isb();

    // TTBR0 -> L0
    __d_println(DEBUG, "setup el1 tbr0");
    write_ttbr0_el1((uint64_t)L0);

    __d_println(DEBUG, "disabling interrupts");
    dsb_ish();
    isb();

    // Invalidate TLBs before turning on MMU
    __d_println(DEBUG, "invalidating tlbs and enable mmu");
    tlbi_all();
    dsb_ish();
    isb();

    __d_println(DEBUG, "enabling caches");
    // Enable caches + MMU (and keep alignment checks off unless you want them)
    uint64_t sctlr = read_sctlr_el1();
    sctlr |= (SCTLR_I | SCTLR_C | SCTLR_M);
    // Optionally: sctlr |= (SCTLR_A | SCTLR_SA);
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
