#ifndef __KERNEL_SUBSYSTEMS_IO_BLOCK_BLOCK_CACHE_H
#define __KERNEL_SUBSYSTEMS_IO_BLOCK_BLOCK_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#include "block_device.h"
#include "../../../types/fixed_array.h"
#include "../../../mutex.h"

#define BLOCK_CACHE_MAX_ENTRIES 64

typedef struct BlockCacheEntry
{
    BlockNumber block;
    Data data[BLOCK_DEVICE_BLOCK_SIZE];
}
BlockCacheEntry;

typedef BlockCacheEntry * BlockCacheEntryPtr;

OptionalDef(BlockCacheEntry);
bool BlockCacheEntry__equals(BlockCacheEntryPtr self, BlockCacheEntryPtr other);
FixedArrayDefine(BlockCacheEntry, BLOCK_CACHE_MAX_ENTRIES, BlockCacheEntries, (EqualFunction) BlockCacheEntry__equals);

typedef struct BlockCache
{
    BlockDevicePtr device;
    BlockCacheEntries entries;
    mutex_t lock;
}
BlockCache;

typedef BlockCache * BlockCachePtr;

BlockCache BlockCache__new(BlockDevicePtr device);
Result(BlockDeviceCode) BlockCache__read(BlockCachePtr self, BlockNumber block, DataPtr target);

#endif
