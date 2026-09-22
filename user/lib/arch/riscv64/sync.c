#include <stddef.h>

void __sync_boundary(void)
{
    __asm__ __volatile__("fence rw, rw" ::: "memory");
}

void __sync_cache_clean(void * address, size_t size)
{
    __asm__ __volatile__("fence rw, rw" ::: "memory");
}

void __sync_cache_invalidate(void * address, size_t size)
{
    __asm__ __volatile__("fence rw, rw" ::: "memory");
}

void __sync_cache_clean_invalidate(void * address, size_t size)
{
    __asm__ __volatile__("fence rw, rw" ::: "memory");
}
