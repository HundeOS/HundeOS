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

#define BLOCK_TABLE_SIZE 256
#define BLOCK_BUCKET_SIZE 256

typedef struct MemoryBlock
{
    KernelAddress start;
    mutex_t block_lock;
    uint64_t pages_allocated;
} MemoryBlock;

typedef MemoryBlock * MemoryBlockPtr;

OptionalDef(MemoryBlock);
ResultDef(MemoryBlock);

HashCode Heap__hash_address(Address * self);

HashtableDefine(
    Address,
    MemoryBlock,
    BLOCK_TABLE_SIZE,
    BLOCK_BUCKET_SIZE,
    AllocatedBlocks,
    Heap__hash_address,
    SimpleEquals(uint64_t),
    never__equals
);

typedef struct Heap
{
    mutex_t lock;
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
KernelAddress Heap__try_from_virtual_address(HeapPtr self, Address virtual);

#define NEW_BLOCK \
    { \
        .start = INVALID_KERNEL_ADDR, \
        .block_lock = MUTEX_INIT, \
        .pages_allocated = 0,\
    } \


#define NEW_HEAP \
    (Heap) { \
        .lock = MUTEX_INIT, \
        .start = 0, \
        .end = 0, \
        .phys_start = 0,\
        .current_block = NEW_BLOCK, \
        .allocated_blocks = EmptyHashtable(AllocatedBlocks),\
        .free_blocks = EmptyHashtable(AllocatedBlocks),\
    } \

#endif
