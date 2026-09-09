#define barrier_dsb_sy() __asm__ volatile("dsb sy" ::: "memory")
#define barrier_isb_sy() __asm__ volatile("isb" ::: "memory")

#include <stddef.h>
#include <stdint.h>

static uint64_t Sync__cache_line_size(void)
{
    uint64_t ctr_el0;
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr_el0));
    return 4ULL << ((ctr_el0 >> 16) & 0xf);
}

static uintptr_t Sync__cache_line_start(void * address, uint64_t line_size)
{
    return ((uintptr_t)address) & ~((uintptr_t)line_size - 1);
}

static uintptr_t Sync__cache_line_end(void * address, size_t size, uint64_t line_size)
{
    return ((uintptr_t)address + size + line_size - 1) & ~((uintptr_t)line_size - 1);
}

void __sync_boundary()
{
    __sync_synchronize();

    barrier_dsb_sy();
    barrier_isb_sy();
}

void __sync_cache_clean(void * address, size_t size)
{
    uint64_t line_size = Sync__cache_line_size();
    uintptr_t start = Sync__cache_line_start(address, line_size);
    uintptr_t end = Sync__cache_line_end(address, size, line_size);

    for (uintptr_t cursor = start; cursor < end; cursor += line_size)
    {
        __asm__ __volatile__("dc cvac, %0" ::"r"(cursor) : "memory");
    }

    barrier_dsb_sy();
}

void __sync_cache_invalidate(void * address, size_t size)
{
    uint64_t line_size = Sync__cache_line_size();
    uintptr_t start = Sync__cache_line_start(address, line_size);
    uintptr_t end = Sync__cache_line_end(address, size, line_size);

    for (uintptr_t cursor = start; cursor < end; cursor += line_size)
    {
        __asm__ __volatile__("dc civac, %0" ::"r"(cursor) : "memory");
    }

    barrier_dsb_sy();
    barrier_isb_sy();
}

void __sync_cache_clean_invalidate(void * address, size_t size)
{
    uint64_t line_size = Sync__cache_line_size();
    uintptr_t start = Sync__cache_line_start(address, line_size);
    uintptr_t end = Sync__cache_line_end(address, size, line_size);

    for (uintptr_t cursor = start; cursor < end; cursor += line_size)
    {
        __asm__ __volatile__("dc civac, %0" ::"r"(cursor) : "memory");
    }

    barrier_dsb_sy();
    barrier_isb_sy();
}
