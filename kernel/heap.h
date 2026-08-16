#ifndef __KERNEL_HEAP_H
#define __KERNEL_HEAP_H
#include "all_defs.h"
#include "address.h"
#include "types/types.h"
#include "types/result.h"
#include "types/optional.h"
#include "types/hashtable.h"
#include "mutex.h"
#include "memory_page.h"
#include "system_info.h"

#define BUCKET_SIZE 1024
#define TABLE_SIZE (MAX_BLOCKS / BUCKET_SIZE)

typedef struct MemoryBlock
{
    KernelAddress start;
    mutex_t block_lock;
    uint64_t pages_allocated;
} MemoryBlock;

typedef MemoryBlock * MemoryBlockPtr;

OptionalDef(MemoryBlock);
ResultDef(MemoryBlock);

HashtableDefine(Address, MemoryBlock, TABLE_SIZE, BUCKET_SIZE, AllocatedBlocks, hash__uint64, SimpleEquals(uint64_t), never__equals);

typedef struct Heap
{
    Address start;
    Address end;
    Address phys_start;
    MemoryBlock current_block;
    AllocatedBlocks allocated_blocks;
    AllocatedBlocks free_blocks;
}
Heap;

typedef enum HeapCodes
{
    HEAP_NOT_IMPLEMENTED = -1,
    HEAP_OK = 0,
}
HeapCodes;

typedef Heap * HeapPtr;
typedef uint64_t HeapCode;

ResultDef(HeapCode);
Result(HeapCode) Heap__init(HeapPtr self, SystemInfoPtr system_info);

KernelAddress Heap__alloc(HeapPtr self);
KernelAddress Heap__alloc_size(HeapPtr self, size_t size);

Result(HeapCode) Heap__free(HeapPtr self, KernelAddress addr);

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
        .allocated_blocks = EmptyHashtable(AllocatedBlocks),\
        .free_blocks = EmptyHashtable(AllocatedBlocks),\
    } \

#endif
