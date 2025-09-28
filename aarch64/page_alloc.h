#ifndef __AARCH64__PAGE_ALLOC_H
#define __AARCH64__PAGE_ALLOC_H
#include <stdint.h>

#define PAGE_SIZE (4 * 1024)

extern char __heap_start[];

typedef struct MemoryPage {
    char page[PAGE_SIZE];
} MemoryPage;

typedef struct FreeMemoryPage {
    struct FreeMemoryPage * next;
} FreeMemoryPage;

MemoryPage * MemoryPage__alloc();
void MemoryPage__free(MemoryPage * page);

#endif
