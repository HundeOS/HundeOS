#include "mem.h"

#include <stddef.h>
#include <stdint.h>

#include "syscall.h"

void * mem_alloc_pages(size_t size)
{
    if (size == 0)
    {
        return NULL;
    }

    MemAddress result = syscall(MEM_CALL, MEM_ALLOC_PAGES, size, 0, 0, 0);
    if ((int64_t) result <= 0)
    {
        return NULL;
    }

    return (void *) result;
}

MemStatus mem_free_pages(void * address)
{
    return (MemStatus) syscall(MEM_CALL, MEM_FREE_PAGES, (MemAddress) address, 0, 0, 0);
}

void * _sbrk(ptrdiff_t increment)
{
    MemAddress result = syscall(MEM_CALL, MEM_SBRK, (MemIncrement) increment, 0, 0, 0);
    if ((int64_t) result < 0)
    {
        return (void *) -1;
    }

    return (void *) result;
}

void * _sbrk_r(ptrdiff_t increment)
{
    return _sbrk(increment);
}
