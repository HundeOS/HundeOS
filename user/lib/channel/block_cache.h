#ifndef HUND_USER_CHANNEL_BLOCK_CACHE_H
#define HUND_USER_CHANNEL_BLOCK_CACHE_H

#include "block_client.h"
#include "types/cache.h"

#define BLOCK_CACHE_CAPACITY 1024U

typedef struct BlockCacheSector
{
    uint8_t data[BLOCK_CHANNEL_SECTOR_SIZE];
} BlockCacheSector;

CacheDefine(BlockProtocolBlockNumber, BlockCacheSector, BLOCK_CACHE_CAPACITY, BlockCacheSectors);

typedef struct BlockCache
{
    BlockCacheSectors sectors;
} BlockCache;

typedef BlockCache * BlockCachePtr;

BlockCache BlockCache__new(void);
Result(BlockProtocolBlockCount)
    BlockCache__read(BlockCachePtr self, BlockClientPtr client, BlockProtocolBlockNumber block,
                     BlockProtocolBlockCount block_count, Address buffer, size_t buffer_size);
Result(BlockProtocolBlockCount)
    BlockCache__write(BlockCachePtr self, BlockClientPtr client, BlockProtocolBlockNumber block,
                      BlockProtocolBlockCount block_count, Address buffer, size_t buffer_size);

#endif
