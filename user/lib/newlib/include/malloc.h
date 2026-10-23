#ifndef __USER_LIB_NEWLIB_MALLOC_H
#define __USER_LIB_NEWLIB_MALLOC_H

#include <stddef.h>

struct mallinfo
{
    size_t arena;
    size_t ordblks;
    size_t smblks;
    size_t hblks;
    size_t hblkhd;
    size_t usmblks;
    size_t fsmblks;
    size_t uordblks;
    size_t fordblks;
    size_t keepcost;
};

void * malloc(size_t size);
void free(void * ptr);
void * calloc(size_t n, size_t elem);
void * realloc(void * ptr, size_t size);
size_t malloc_usable_size(void * ptr);
void * memalign(size_t alignment, size_t size);

#endif
