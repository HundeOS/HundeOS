#ifndef __AARCH64_BOOT_H_
#define __AARCH64_BOOT_H_
#include <stdint.h>
#include "../../../kernel/system_info.h"

#define MIMO_HIGH_END MIMO_HIGH_BASE + 4 * 1024 * 1024
#define MAX_KERNEL_SIZE 4 * 1024 * 1024

#define KERNEL_HIGH_VIRT_BASE (uint64_t)0xFFFF000000000000ULL

#ifdef __MACH__
#define __BOOT__(a) a
#define __BOOT_RODATA__(a) a
#define __BOOT_DATA__(a) a
#define __BOOT_PAGE_TABLE__(a) a
#define __BOOT_BSS__(a) a
#define __BOOT_ALLOCATION__(a) a
#else
#define __BOOT__(a) a __attribute__((section(".boot.text")))
#define __BOOT_RODATA__(a) a __attribute__((section(".boot.rodata")))
#define __BOOT_DATA__(a) a __attribute__((section(".boot.data"))) __attribute__((aligned(4096)))
#define __BOOT_PAGE_TABLE__(a) a __attribute__((section(".boot.data.table"))) __attribute__((aligned(4096)))
#define __BOOT_BSS__(a) a __attribute__((section(".boot.bss")))
#define __BOOT_ALLOCATION__(a) a __attribute__((section(".boot.allocation"))) __attribute__((aligned(4096)))
#endif
#define __BOOT_STRING__(name) __BOOT_RODATA__(static const char name[])

// Masks for addresses inside descriptors
#define PTE_NEXTADDR_MASK 0x0000FFFFFFFFF000ULL  // [47:12]
#define PTE_OFFSET_MASK 0x0000000000000FFFULL    // [47:12]
#define L1_BLOCK_PA_MASK 0x0000FFFFC0000000ULL   // [47:30] (1 GiB)
#define L2_BLOCK_PA_MASK 0x0000FFFFFFFFE00000ULL // [47:21] (2 MiB)

#define PAGE_POOL_SIZE (2048)

// Index extractors for 48-bit VA, 4K granule
inline unsigned va_idx_l0(uint64_t va)
{
    return (va >> 39) & 0x1FF;
}

inline unsigned va_idx_l1(uint64_t va)
{
    return (va >> 30) & 0x1FF;
}

inline unsigned va_idx_l2(uint64_t va)
{
    return (va >> 21) & 0x1FF;
}

inline unsigned va_idx_l3(uint64_t va)
{
    return (va >> 12) & 0x1FF;
}

// Offsets within a leaf
inline uint64_t va_off_1g(uint64_t va)
{
    return va & ((1ULL << 30) - 1);
} // 1 GiB

inline uint64_t va_off_2m(uint64_t va)
{
    return va & ((1ULL << 21) - 1);
} // 2 MiB

inline uint64_t va_off_4k(uint64_t va)
{
    return va & ((1ULL << 12) - 1);
} // 4 KiB

inline uint64_t rd_esr_el1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(v));
    return v;
}

inline uint64_t rd_elr_el1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, elr_el1" : "=r"(v));
    return v;
}

inline uint64_t rd_far_el1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, far_el1" : "=r"(v));
    return v;
}

inline uint64_t rd_spsr_el1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, spsr_el1" : "=r"(v));
    return v;
}

inline uint64_t rd_tcr(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(v));
    return v;
}

inline void wr_tcr(uint64_t v)
{
    __asm__ volatile("msr tcr_el1, %0\nisb" ::"r"(v) : "memory");
}

inline void tlbi_vmalle1(void)
{
    __asm__ volatile("tlbi vmalle1");
}

inline void write_vbar_el1(uint64_t v)
{
    __asm__ volatile("msr vbar_el1,%0" ::"r"(v));
}

inline unsigned idx0(uint64_t va)
{
    return (va >> 39) & 0x1FF;
}

inline unsigned idx1(uint64_t va)
{
    return (va >> 30) & 0x1FF;
}

inline unsigned idx2(uint64_t va)
{
    return (va >> 21) & 0x1FF;
}

inline unsigned idx3(uint64_t va)
{
    return (va >> 12) & 0x1FF;
}

extern void enter_kernel(uint64_t stack, uint64_t vectors, uint64_t entry, SystemInfoPtr system_info);
void enable_fp_simd(void);
void enable_tbi_for_kernel(void);

enum
{
    PTE_VALID = 1ull << 0,
    PTE_TABLE = 1ull << 1, // when VALID=1
    PTE_AF = 1ull << 10,
    PTE_UXN = 1ull << 54,
    PTE_PXN = 1ull << 53,
};

void secondary_booted(uint64_t cpu);

#endif
