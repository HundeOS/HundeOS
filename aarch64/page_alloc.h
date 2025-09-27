#ifndef __AARCH64__PAGE_ALLOC_H
#define __AARCH64__PAGE_ALLOC_H
#include <stdint.h>

#define PAGE_SIZE   0x1000ULL

extern char __page_pool_start[];
extern char __page_pool_end[];

typedef uint64_t * MemoryPage;

MemoryPage MemoryPage__alloc();

#endif
