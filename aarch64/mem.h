#ifndef __AARCH64_MEM_H
#define __AARCH64_MEM_H

// MAIR index meanings we set:
//  AttrIdx 0 = Normal, Inner/Outer WB WA, non-transient (0xFF)
//  AttrIdx 1 = Device-nGnRE (0x04)
#define MAIR_ATTR_NORMAL_WBWA  0xFFULL
#define MAIR_ATTR_DEVICE_nGnRE 0x04ULL
#define MAIR_VALUE  ((MAIR_ATTR_NORMAL_WBWA << 0) | (MAIR_ATTR_DEVICE_nGnRE << 8))

// TCR configuration: 48-bit VA space (T0SZ=16), 4KB TG0, 40-bit PA (IPS=0b010),
// Inner/Outer WBWA, Inner Shareable
#define TCR_T0SZ(n)         ((uint64_t)(n)        )        // bits [5:0]
#define TCR_T1SZ(n)         ((uint64_t)(n) << 16)          // bits [21:16]

#define TCR_EPD0            (1ull << 7)    // 1 = disable TTBR0 walks
#define TCR_EPD1            (1ull << 23)   // 1 = disable TTBR1 walks
// Use 0 to ENABLE each side:
#define TCR_EPD0_EN         (0ull << 7)
#define TCR_EPD1_EN         (0ull << 23)

#define TCR_IRGN0_WBWA      (1ull << 8)    // 01b
#define TCR_ORGN0_WBWA      (1ull << 10)
#define TCR_SH0_IS          (3ull << 12)   // 11b = Inner Shareable
// TG0 (bits [15:14]): 00b = 4 KiB
#define TCR_TG0_4K          (0ull << 14)

#define TCR_IRGN1_WBWA      (1ull << 24)
#define TCR_ORGN1_WBWA      (1ull << 26)
#define TCR_SH1_IS          (3ull << 28)
// TG1 (bits [31:30]): **10b = 4 KiB** (note the different encoding vs TG0)
#define TCR_TG1_4K          (2ull << 30)

#define TCR_IPS_40BIT       (2ull << 32)   // 40-bit PA space on QEMU virt

#define TCR_VALUE ( \
    TCR_IPS_40BIT | TCR_TG0_4K | TCR_SH0_IS | TCR_ORGN0_WBWA | TCR_IRGN0_WBWA | \
    TCR_EPD0_EN | TCR_EPD1_EN | TCR_T0SZ(16) \
)

// SCTLR bits
#define SCTLR_M     (1ULL << 0)
#define SCTLR_A     (1ULL << 1)  // (optional) alignment check
#define SCTLR_C     (1ULL << 2)
#define SCTLR_SA    (1ULL << 3)
#define SCTLR_SA0   (1ULL<<4)
#define SCTLR_I     (1ULL << 12)
#define SCTLR_WXN   (1ULL<<19)
#define SCTLR_RES1  ((1ULL<<11)|(1ULL<<20)|(1ULL<<22)|(1ULL<<23)|(1ULL<<28)|(1ULL<<29))



inline uint64_t boot_tcr_el1(void) {
    uint64_t tcr = (2ull << 32) |                // IPS=40-bit PA
    (0ull << 14) |                // TG0 = 4K
    (3ull << 12) |                // SH0 = Inner Shareable
    (1ull << 10) | (1ull << 8) |  // ORGN0/IRGN0 = WBWA
    (1ull << 30) |                // TG1 = 4K (encoded separately)
    (3ull << 28) |                // SH1 = Inner Shareable
    (1ull << 26) | (1ull << 24) | // ORGN1/IRGN1 = WBWA
    (0ull << 7)  |                // EPD0=0 (enable TTBR0)
    (0ull << 23) |                // EPD1=0 (enable TTBR1)
    (16ull /*T0SZ*/) |
    (16ull << 16) /*T1SZ*/;

    return TCR_IPS_40BIT
         | TCR_TG0_4K | TCR_SH0_IS | TCR_ORGN0_WBWA | TCR_IRGN0_WBWA
         | TCR_TG1_4K | TCR_SH1_IS | TCR_ORGN1_WBWA | TCR_IRGN1_WBWA
         | TCR_EPD0_EN | TCR_EPD1_EN
         | TCR_T0SZ(16) | TCR_T1SZ(16);
}

inline uint64_t align_down_2m(uint64_t x)
{
    return x & ~((1ULL<<21)-1);
}

inline uint64_t align_up_2m(uint64_t x)
{
    return (x + ((1ULL<<21)-1)) & ~((1ULL<<21)-1);
}


void MMU__init(void);
void MMU__secondary_init(void);

#endif