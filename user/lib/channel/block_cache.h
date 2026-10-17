#ifndef HUND_USER_CHANNEL_BLOCK_CACHE_H
#define HUND_USER_CHANNEL_BLOCK_CACHE_H

#include "block_client.h"
#include "types/cache.h"

#define BLOCK_CACHE_CAPACITY 1024U

typedef struct BlockCacheSector
{
    uint8_t data[BLOCK_CHANNEL_SECTOR_SIZE];
} BlockCacheSector;

OptionalDef(BlockCacheSector);

CacheDefine(BlockProtocolBlockNumber, BlockCacheSector, BLOCK_CACHE_CAPACITY, BlockCacheSectors);

typedef struct BlockCacheChunk BlockCacheChunk;
typedef BlockCacheChunk * BlockCacheChunkPtr;
OptionalDef(BlockCacheChunkPtr);

struct BlockCacheChunk
{
    BlockCacheSectors sectors;
    Optional(BlockCacheChunkPtr) next;
};

typedef struct BlockCache
{
    BlockCacheSectors sectors;
    Optional(BlockCacheChunkPtr) chunks;
} BlockCache;

typedef BlockCache * BlockCachePtr;

BlockCache BlockCache__new(void);
void BlockCache__reclaim(BlockCachePtr self);
uint64_t BlockCache__capacity(BlockCachePtr self);
Result(BlockProtocolBlockCount)
    BlockCache__read(BlockCachePtr self, BlockClientPtr client, BlockProtocolBlockNumber block,
                     BlockProtocolBlockCount block_count, Address buffer, size_t buffer_size);
Result(BlockProtocolBlockCount)
    BlockCache__write(BlockCachePtr self, BlockClientPtr client, BlockProtocolBlockNumber block,
                      BlockProtocolBlockCount block_count, Address buffer, size_t buffer_size);

#endif
