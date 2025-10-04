#ifndef __AARCH64_MEM_SMP_H
#define __AARCH64_MEM_SMP_H
#include <stdint.h>

// Provided by your earlier code:
extern uint64_t L0[]; // top-level table base used by the primary

inline void isb(void){ __asm__ volatile("isb"); }
inline void dsb_ish(void){ __asm__ volatile("dsb ish"); }
inline void tlbi_all(void){ __asm__ volatile("tlbi vmalle1"); }

inline void write_mair_el1(uint64_t v){ __asm__ volatile("msr mair_el1, %0"::"r"(v)); }
inline void write_tcr_el1(uint64_t v){ __asm__ volatile("msr tcr_el1, %0"::"r"(v)); }
inline void write_ttbr0_el1(uint64_t v){ __asm__ volatile("msr ttbr0_el1, %0"::"r"(v)); }
inline uint64_t read_sctlr_el1(void){ uint64_t v; __asm__ volatile("mrs %0, sctlr_el1":"=r"(v)); return v; }
inline void write_sctlr_el1(uint64_t v){ __asm__ volatile("msr sctlr_el1, %0"::"r"(v)); }

// Use the same values you used on the boot CPU:
#define MAIR_VALUE   (0xFFull << 0 /*Attr0 Normal WBWA*/ | 0x04ull << 8 /*Attr1 Device-nGnRE*/)
#define TCR_VALUE    ((2ull<<32) /*IPS=40b*/ | (0ull<<14) /*TG0=4K*/ | (3ull<<12) /*SH0 IS*/ | (1ull<<10) /*ORGN0 WBWA*/ | (1ull<<8) /*IRGN0 WBWA*/ | (16ull /*T0SZ*/))
#define SCTLR_I (1ull<<12)
#define SCTLR_C (1ull<<2)
#define SCTLR_M (1ull<<0)

static inline void mmu_secondary_init(void)
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

#endif