#include "../../kernel/arch/cache.h"

#include <stdint.h>

#include "../../kernel/all_defs.h"

static uint64_t Arch__cache_line_size(void)
{
    uint64_t ctr_el0;
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr_el0));
    return 4ULL << ((ctr_el0 >> 16) & 0xf);
}

void Arch__sync_instruction_memory(Address address, size_t size)
{
    uint64_t line_size = Arch__cache_line_size();
    Address start = address & ~(line_size - 1);
    Address end = (address + size + line_size - 1) & ~(line_size - 1);

    for (Address cursor = start; cursor < end; cursor += line_size)
    {
        __asm__ __volatile__("dc cvau, %0" ::"r"(cursor) : "memory");
    }

    __asm__ __volatile__("dsb ish" ::: "memory");

    for (Address cursor = start; cursor < end; cursor += line_size)
    {
        __asm__ __volatile__("ic ivau, %0" ::"r"(cursor) : "memory");
    }

    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}
