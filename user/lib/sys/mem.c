#include "mem.h"

#include <stddef.h>
#include <stdint.h>

#include "syscall.h"

static BString mem_allocation_failed = bstr("Memory allocation failed");
static BString mem_invalid_size = bstr("Invalid memory allocation size");

void * mem_alloc_pages(size_t size)
{
    if (size == 0)
    {
        return NULL; // hundelint: ignore-no-null
    }

    MemAddress result = syscall(MEM_CALL, MEM_ALLOC_PAGES, size, 0, 0, 0);
    if ((int64_t)result <= 0)
    {
        return NULL; // hundelint: ignore-no-null
    }

    return (void *)result;
}

Result(Anything) mem_alloc_bytes(size_t size)
{
    if (size == 0)
    {
        return Error(Anything, mem_invalid_size, MEM_INVALID_SIZE);
    }

    void * allocation = malloc(size);
    if (allocation == 0) // hundelint: ignore-no-null
    {
        return Error(Anything, mem_allocation_failed, MEM_ALLOCATION_FAILED);
    }

    return Success(Anything, (Anything)allocation);
}

Result(Anything) mem_alloc_pages_result(size_t size)
{
    if (size == 0)
    {
        return Error(Anything, mem_invalid_size, MEM_INVALID_SIZE);
    }

    void * allocation = mem_alloc_pages(size);
    if (allocation == 0) // hundelint: ignore-no-null
    {
        return Error(Anything, mem_allocation_failed, MEM_ALLOCATION_FAILED);
    }

    return Success(Anything, (Anything)allocation);
}

MemStatus mem_free_pages(void * address)
{
    return (MemStatus)syscall(MEM_CALL, MEM_FREE_PAGES, (MemAddress)address, 0, 0, 0);
}

Result(Anything) mem_alloc_shared_page(uint64_t first_peer, uint64_t second_peer)
{
    int64_t result = (int64_t)syscall(MEM_CALL, MEM_ALLOC_SHARED_PAGE, first_peer, second_peer, 0, 0);
    if (result < 0)
    {
        return Error(Anything, mem_allocation_failed, result);
    }
    return Success(Anything, (Anything)result);
}

MemStatus mem_release_shared_page(uint64_t address)
{
    return (MemStatus)syscall(MEM_CALL, MEM_RELEASE_SHARED_PAGE, address, 0, 0, 0);
}

void * _sbrk(ptrdiff_t increment)
{
    MemAddress result = syscall(MEM_CALL, MEM_SBRK, (MemIncrement)increment, 0, 0, 0);
    if ((int64_t)result < 0)
    {
        return (void *)-1;
    }

    return (void *)result;
}

void * _sbrk_r(ptrdiff_t increment)
{
    return _sbrk(increment);
}
