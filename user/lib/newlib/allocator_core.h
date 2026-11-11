#ifndef HUNDE_NEWLIB_ALLOCATOR_CORE_H
#define HUNDE_NEWLIB_ALLOCATOR_CORE_H

#include <stddef.h>

void * Allocator__malloc_unlocked(size_t size);
void Allocator__free_unlocked(void * allocation);
void * Allocator__calloc_unlocked(size_t count, size_t size);
void * Allocator__realloc_unlocked(void * allocation, size_t size);
size_t Allocator__usable_size_unlocked(void * allocation);
void * Allocator__memalign_unlocked(size_t alignment, size_t size);

#endif
