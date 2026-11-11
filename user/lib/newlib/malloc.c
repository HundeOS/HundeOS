#include <stddef.h>

#include "allocator.h"
#include "allocator_core.h"
#include "../sys/user_mutex.h"

static UserMutex allocator_mutex;

void Allocator__lock(void)
{
    if (UserMutex__lock(&allocator_mutex) != THREAD_WAIT_WOKEN)
    {
        __builtin_trap();
    }
}

void Allocator__unlock(void)
{
    if (UserMutex__unlock(&allocator_mutex) != THREAD_WAIT_WOKEN)
    {
        __builtin_trap();
    }
}

void * malloc(size_t size)
{
    Allocator__lock();
    void * allocation = Allocator__malloc_unlocked(size);
    Allocator__unlock();
    return allocation;
}

void free(void * allocation)
{
    Allocator__lock();
    Allocator__free_unlocked(allocation);
    Allocator__unlock();
}

void * calloc(size_t count, size_t size)
{
    Allocator__lock();
    void * allocation = Allocator__calloc_unlocked(count, size);
    Allocator__unlock();
    return allocation;
}

void * realloc(void * allocation, size_t size)
{
    Allocator__lock();
    void * resized = Allocator__realloc_unlocked(allocation, size);
    Allocator__unlock();
    return resized;
}

size_t malloc_usable_size(void * allocation)
{
    Allocator__lock();
    size_t size = Allocator__usable_size_unlocked(allocation);
    Allocator__unlock();
    return size;
}

void * memalign(size_t alignment, size_t size)
{
    Allocator__lock();
    void * allocation = Allocator__memalign_unlocked(alignment, size);
    Allocator__unlock();
    return allocation;
}
