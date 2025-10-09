#include <kernel.h>

#define _disable_mmu() \
    uint64_t saved_sctlr; \
\
    __asm__ volatile( \
        "msr sctlr_el1, %0\n" \
        "isb\n" \
        : : "r" (saved_sctlr & ~1ULL) \
    );

#define _enable_mmu() \
    __asm__ volatile( \
        "msr sctlr_el1, %0\n" \
        "isb\n" \
        : : "r" (saved_sctlr) \
    );

void dk_println(const char * str) {
    _disable_mmu();
    k_println(str);
    _enable_mmu();
}

void dk_printf(const char * str, uint64_t val) {
    _disable_mmu();
    k_printf(str, val);
    _enable_mmu();
}
