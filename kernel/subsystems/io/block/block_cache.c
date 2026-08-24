#include "block_cache.h"

FixedArrayBodyDefine(BlockCacheEntries);

static BString invalid_argument = BSTRING_NEW("Invalid block cache argument");

bool BlockCacheEntry__equals(BlockCacheEntryPtr self, BlockCacheEntryPtr other)
{
    return self->block == other->block;
}

BlockCache BlockCache__new(BlockDevicePtr device)
{
    return (BlockCache) {
        .device = device,
        .entries = BlockCacheEntries__new(),
        .lock = MUTEX_INIT,
    };
}

static bool BlockCache__block_matches(BlockCacheEntryPtr entry, void * context)
{
    BlockNumber block = *(BlockNumber *) context;
    return entry->block == block;
}

static void BlockCache__copy(DataPtr target, DataPtr source)
{
    for (size_t i = 0; i < BLOCK_DEVICE_BLOCK_SIZE; i++)
    {
        target[i] = source[i];
    }
}

static Result(BlockDeviceCode) BlockCache__cache_entry(BlockCachePtr self, BlockCacheEntry entry)
{
    if (BlockCacheEntries__len(&self->entries) >= BLOCK_CACHE_MAX_ENTRIES)
    {
        BlockCacheEntries__pop_front(&self->entries);
    }

    match_result(CollectionCode, BlockCacheEntries__push(&self->entries, entry),
        success(_) { return Success(BlockDeviceCode, BLOCK_DEVICE_OK); },
        error(code, message) { return Error(BlockDeviceCode, message, BLOCK_DEVICE_IO_ERROR); }
    );

    return Error(BlockDeviceCode, invalid_argument, BLOCK_DEVICE_IO_ERROR);
}

Result(BlockDeviceCode) BlockCache__read(BlockCachePtr self, BlockNumber block, DataPtr target)
{
    if (self == NULL || self->device == NULL || target == NULL) // hundelint: ignore-no-null
    {
        return Error(BlockDeviceCode, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    BlockCacheEntry entry;
    entry.block = block;

    critical_section(self->lock,
    {
        match_optional(BlockCacheEntries__FindResult, BlockCacheEntries__find(&self->entries, BlockCache__block_matches, &block),
            some(found)
            {
                BlockCache__copy(target, found.value->data);
                cs_return Success(BlockDeviceCode, BLOCK_DEVICE_OK);
            },
            none_do_nothing()
        );

        match_result(BlockCount, BlockDevice__read(self->device, block, 1, entry.data),
            success(count)
            {
                if (count != 1)
                {
                    cs_return Error(BlockDeviceCode, invalid_argument, BLOCK_DEVICE_IO_ERROR);
                }
            },
            error(code, message) { cs_return Error(BlockDeviceCode, message, code); }
        );

        match_result(BlockDeviceCode, BlockCache__cache_entry(self, entry),
            success_do_nothing(),
            error(code, message) { cs_return Error(BlockDeviceCode, message, code); }
        );

        BlockCache__copy(target, entry.data);
        cs_return Success(BlockDeviceCode, BLOCK_DEVICE_OK);
    });
}
