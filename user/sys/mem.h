#ifndef __USER_SYS_MEM_H
#define __USER_SYS_MEM_H

#include <stddef.h>

#include "syscalls/mem.h"

void * mem_alloc_pages(size_t size);
MemStatus mem_free_pages(void * address);
void * _sbrk(ptrdiff_t increment);
void * _sbrk_r(ptrdiff_t increment);
void * malloc(size_t size);
void free(void * address);

#endif
