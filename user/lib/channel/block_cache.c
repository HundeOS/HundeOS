#include "block_cache.h"

#include "memory.h"

#include "../../../common/ipc/io_diagnostics.h"

static bool BlockCache__keys_equal(const BlockProtocolBlockNumber * first, const BlockProtocolBlockNumber * second)
{
    return *first == *second;
}

CacheBodyDefine(BlockCacheSectors, BlockCache__keys_equal);

static void BlockCache__store(BlockCachePtr self, BlockProtocolBlockNumber block, const uint8_t * data)
{
    BlockCacheSector sector;
    Mem__memcpy(sector.data, data, sizeof(sector.data));
    if (BlockCacheSectors__put(&self->sectors, block, sector) == CACHE_EVICTED)
    {
        IODiagnostics__record(IO_DIAGNOSTIC_BLOCK_CACHE_EVICTIONS, 1);
    }
}

static void BlockCache__update_resident(BlockCachePtr self, BlockProtocolBlockNumber block, const uint8_t * data)
{
    BlockCacheSector sector;
    Mem__memcpy(sector.data, data, sizeof(sector.data));
    BlockCacheSectors__replace(&self->sectors, block, sector);
}

BlockCache BlockCache__new(void)
{
    return (BlockCache){.sectors = BlockCacheSectors__new()};
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
            BlockCacheSectors__get(&self->sectors, current),
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
                BlockCacheSectors__get(&self->sectors, block + index),
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
