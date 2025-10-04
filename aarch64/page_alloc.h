#ifndef __AARCH64__PAGE_ALLOC_H
#define __AARCH64__PAGE_ALLOC_H

#include <stdbool.h>

#include "asm_helpers.h"

#define PAGE_SIZE (4 * 1024)
#define _2MB (2 * 1024 * 1024)
#define COARSE_BLOCK_SIZE _2MB
#define PAGES_PER_BLOCK (COARSE_BLOCK_SIZE / PAGE_SIZE)

#define __BOOTSTRAP__(a) __attribute__((section(".bss.heap"))) __attribute__(( aligned(_2MB))) a

extern char __heap_start[];
extern char __heap_start_phys[];
extern char __heap_end_phys[];

typedef uint64_t Address;

typedef uint64_t Address;

typedef struct KernelAddress {
    Address virtual_addr;
    Address phys_addr;
} KernelAddress;

typedef struct MemoryPage {
    char page[PAGE_SIZE];
} MemoryPage;

typedef struct MemoryBlock {
    KernelAddress start;
    uint64_t pages_allocated;
    bool bootstrapped;
} MemoryBlock;

MemoryPage * MemoryPage__alloc();

void MemoryPage__free(MemoryPage * page);

inline MemoryPage * MemoryPage__zero(MemoryPage * self){
    for (int i = 0; i < PAGE_SIZE; i++) {
        self->page[i] = 0;
    }

    dsb_ish();
    return self;
}


#endif
