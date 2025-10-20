#ifndef __KERNEL_HEAP_H
#define __KERNEL_HEAP_H
#include "address.h"
#include "types/types.h"
#include "mutex.h"
#include "memory_page.h"

#define COARSE_BLOCK_SIZE _4MB
#define PAGES_PER_BLOCK (COARSE_BLOCK_SIZE / PAGE_SIZE)


typedef struct MemoryBlock
{
    KernelAddress start;
    mutex_t block_lock;
    uint64_t pages_allocated;
} MemoryBlock;

typedef MemoryBlock * MemoryBlockPtr;

typedef struct Heap
{
    Address start;
    Address end;
    Address phys_start;
    MemoryBlock current_block;
} Heap;

typedef Heap * HeapPtr;
typedef uint64_t HeapCode;

ResultDef(HeapCode);
Result(HeapCode) Heap__init(HeapPtr self);

KernelAddress Heap__alloc(HeapPtr self);
KernelAddress Heap__alloc_size(HeapPtr self, size_t size);

void Heap__free(HeapPtr self, MemoryPagePtr page);

KernelAddress Heap__from_physical_address(HeapPtr self, Address physical);
KernelAddress Heap__from_virtual_address(HeapPtr self, Address virtual);

#define NEW_BLOCK \
    { \
        .start = INVALID_KERNEL_ADDR, \
        .block_lock = MUTEX_INIT, \
        .pages_allocated = 0,\
    } \


#define NEW_HEAP \
    (Heap) { \
        .start = 0, \
        .end = 0, \
        .phys_start = 0,\
        .current_block = NEW_BLOCK, \
    } \

#endif