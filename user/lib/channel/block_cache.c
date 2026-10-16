#include "block_cache.h"

#include "memory.h"
#include "sys/mem.h"

#include "../../../common/ipc/io_diagnostics.h"

static bool BlockCache__keys_equal(const BlockProtocolBlockNumber * first, const BlockProtocolBlockNumber * second)
{
    return *first == *second;
}

CacheBodyDefine(BlockCacheSectors, BlockCache__keys_equal);

static Optional(BlockCacheSector) BlockCache__get(BlockCachePtr self, BlockProtocolBlockNumber block)
{
    match_optional(
        BlockCacheSectors__get(&self->sectors, block),
        some(sector)
        {
            return Some(BlockCacheSector, sector);
        },
        none_do_nothing()
    );

    Optional(BlockCacheChunkPtr) cursor = self->chunks;
    while (is_something(cursor))
    {
        match_optional(
            cursor,
            some(chunk)
            {
                match_optional(
                    BlockCacheSectors__get(&chunk->sectors, block),
                    some(sector)
                    {
                        return Some(BlockCacheSector, sector);
                    },
                    none_do_nothing()
                );
                cursor = chunk->next;
            },
            none_do_nothing()
        );
    }
    return None(BlockCacheSector);
}

static bool BlockCache__replace(BlockCachePtr self, BlockProtocolBlockNumber block, BlockCacheSector sector)
{
    if (BlockCacheSectors__replace(&self->sectors, block, sector))
    {
        return true;
    }
    Optional(BlockCacheChunkPtr) cursor = self->chunks;
    while (is_something(cursor))
    {
        match_optional(
            cursor,
            some(chunk)
            {
                if (BlockCacheSectors__replace(&chunk->sectors, block, sector))
                {
                    return true;
                }
                cursor = chunk->next;
            },
            none_do_nothing()
        );
    }
    return false;
}

static Optional(BlockCacheChunkPtr) BlockCache__new_chunk(BlockCachePtr self)
{
    match_result(
        mem_alloc_reclaimable_pages_result(sizeof(BlockCacheChunk)),
        success(memory)
        {
            BlockCacheChunkPtr chunk = (BlockCacheChunkPtr)memory;
            chunk->sectors = BlockCacheSectors__new();
            chunk->next = self->chunks;
            self->chunks = Some(BlockCacheChunkPtr, chunk);
            return Some(BlockCacheChunkPtr, chunk);
        },
        error(code, message)
        {
            return None(BlockCacheChunkPtr);
        }
    );
    return None(BlockCacheChunkPtr);
}

static void BlockCache__store(BlockCachePtr self, BlockProtocolBlockNumber block, const uint8_t * data)
{
    BlockCacheSector sector;
    Mem__memcpy(sector.data, data, sizeof(sector.data));
    if (BlockCache__replace(self, block, sector))
    {
        return;
    }
    if (BlockCacheSectors__len(&self->sectors) < BLOCK_CACHE_CAPACITY)
    {
        BlockCacheSectors__put(&self->sectors, block, sector);
        return;
    }
    Optional(BlockCacheChunkPtr) cursor = self->chunks;
    while (is_something(cursor))
    {
        match_optional(
            cursor,
            some(chunk)
            {
                if (BlockCacheSectors__len(&chunk->sectors) < BLOCK_CACHE_CAPACITY)
                {
                    BlockCacheSectors__put(&chunk->sectors, block, sector);
                    return;
                }
                cursor = chunk->next;
            },
            none_do_nothing()
        );
    }
    match_optional(
        BlockCache__new_chunk(self),
        some(chunk)
        {
            BlockCacheSectors__put(&chunk->sectors, block, sector);
            return;
        },
        none_do_nothing()
    );
    if (BlockCacheSectors__put(&self->sectors, block, sector) == CACHE_EVICTED)
    {
        IODiagnostics__record(IO_DIAGNOSTIC_BLOCK_CACHE_EVICTIONS, 1);
    }
}

static void BlockCache__update_resident(BlockCachePtr self, BlockProtocolBlockNumber block, const uint8_t * data)
{
    BlockCacheSector sector;
    Mem__memcpy(sector.data, data, sizeof(sector.data));
    BlockCache__replace(self, block, sector);
}

BlockCache BlockCache__new(void)
{
    return (BlockCache){.sectors = BlockCacheSectors__new(), .chunks = None(BlockCacheChunkPtr)};
}

void BlockCache__reclaim(BlockCachePtr self)
{
    Optional(BlockCacheChunkPtr) cursor = self->chunks;
    self->chunks = None(BlockCacheChunkPtr);
    while (is_something(cursor))
    {
        match_optional(
            cursor,
            some(chunk)
            {
                cursor = chunk->next;
                mem_free_pages(chunk);
            },
            none_do_nothing()
        );
    }
}

uint64_t BlockCache__capacity(BlockCachePtr self)
{
    uint64_t capacity = BLOCK_CACHE_CAPACITY;
    Optional(BlockCacheChunkPtr) cursor = self->chunks;
    while (is_something(cursor))
    {
        match_optional(
            cursor,
            some(chunk)
            {
                capacity += BLOCK_CACHE_CAPACITY;
                cursor = chunk->next;
            },
            none_do_nothing()
        );
    }
    return capacity;
}

Result(BlockProtocolBlockCount)
    BlockCache__read(BlockCachePtr self, BlockClientPtr client, BlockProtocolBlockNumber block,
                     BlockProtocolBlockCount block_count, Address buffer, size_t buffer_size)
{
    if (block_count == 0 || block_count > SIZE_MAX / BLOCK_CHANNEL_SECTOR_SIZE ||
        buffer_size != block_count * BLOCK_CHANNEL_SECTOR_SIZE)
    {
        return BlockClient__read(client, block, block_count, buffer, buffer_size);
    }

    BlockProtocolBlockCount index = 0;
    while (index < block_count)
    {
        BlockProtocolBlockNumber current = block + index;
        match_optional(
            BlockCache__get(self, current),
            some(sector)
            {
                Mem__memcpy((void *)(buffer + index * BLOCK_CHANNEL_SECTOR_SIZE), sector.data, sizeof(sector.data));
                IODiagnostics__record(IO_DIAGNOSTIC_BLOCK_CACHE_HITS, 1);
                index++;
                continue;
            },
            none_do_nothing()
        );

        BlockProtocolBlockCount miss_start = index;
        index++;
        while (index < block_count)
        {
            match_optional(
                BlockCache__get(self, block + index),
                some(sector)
                {
                    break;
                },
                none()
                {
                    index++;
                    continue;
                }
            );
            break;
        }

        BlockProtocolBlockCount miss_count = index - miss_start;
        Address miss_buffer = buffer + miss_start * BLOCK_CHANNEL_SECTOR_SIZE;
        match_result(
            BlockClient__read(client, block + miss_start, miss_count, miss_buffer,
                              miss_count * BLOCK_CHANNEL_SECTOR_SIZE),
            success(transferred)
            {
                if (transferred != miss_count)
                {
                    return Success(BlockProtocolBlockCount, miss_start);
                }
            },
            error(code, message)
            {
                return Error(BlockProtocolBlockCount, message, code);
            }
        );
        for (BlockProtocolBlockCount loaded = 0; loaded < miss_count; loaded++)
        {
            const uint8_t * data = (const uint8_t *)(miss_buffer + loaded * BLOCK_CHANNEL_SECTOR_SIZE);
            BlockCache__store(self, block + miss_start + loaded, data);
        }
        IODiagnostics__record(IO_DIAGNOSTIC_BLOCK_CACHE_MISSES, miss_count);
    }

    return Success(BlockProtocolBlockCount, block_count);
}

Result(BlockProtocolBlockCount)
    BlockCache__write(BlockCachePtr self, BlockClientPtr client, BlockProtocolBlockNumber block,
                      BlockProtocolBlockCount block_count, Address buffer, size_t buffer_size)
{
    match_result(
        BlockClient__write(client, block, block_count, buffer, buffer_size),
        success(transferred)
        {
            if (transferred != block_count)
            {
                return Success(BlockProtocolBlockCount, transferred);
            }
        },
        error(code, message)
        {
            return Error(BlockProtocolBlockCount, message, code);
        }
    );

    for (BlockProtocolBlockCount index = 0; index < block_count; index++)
    {
        const uint8_t * data = (const uint8_t *)(buffer + index * BLOCK_CHANNEL_SECTOR_SIZE);
        BlockCache__update_resident(self, block + index, data);
    }
    return Success(BlockProtocolBlockCount, block_count);
}
