#ifndef __RISCV64_MMU_H
#define __RISCV64_MMU_H

#include "../../kernel/all_defs.h"
#include "../../kernel/arch/mmu.h"

static inline Address align_down_4k(Address value)
{
    return value & ~(PAGE_SIZE - 1ULL);
}

static inline Address align_up_4k(Address value)
{
    return (value + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
}

#endif
