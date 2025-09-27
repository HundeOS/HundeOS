#ifndef __KERNEL_MEM_H
#define __KERNEL_MEM_H
#include <stdint.h>

typedef uint64_t PageTableEntry;
typedef PageTableEntry* PageTable;

typedef struct {
    void* base;
    uint64_t size;
} Stack;
#endif