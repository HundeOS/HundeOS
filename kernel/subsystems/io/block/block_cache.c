#include "block_cache.h"

FixedArrayBodyDefine(BlockCacheEntries);

static KString invalid_argument = KSTRING_NEW("Invalid block cache argument");

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

    ResultMatch(CollectionCode, BlockCacheEntries__push(&self->entries, entry))
        IsSuccess(_, return Success(BlockDeviceCode, BLOCK_DEVICE_OK))
        IsError(code, message, return Error(BlockDeviceCode, message, BLOCK_DEVICE_IO_ERROR))
    End

    return Error(BlockDeviceCode, invalid_argument, BLOCK_DEVICE_IO_ERROR);
}

Result(BlockDeviceCode) BlockCache__read(BlockCachePtr self, BlockNumber block, DataPtr target)
{
    if (self == NULL || self->device == NULL || target == NULL)
    {
        return Error(BlockDeviceCode, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    BlockCacheEntry entry;
    entry.block = block;

    CriticalSection(self->lock,
    {
        FindResultMatch(BlockCacheEntries, BlockCacheEntries__find(&self->entries, BlockCache__block_matches, &block))
            IsSome(found,
            {
                BlockCache__copy(target, found.value->data);
                _cs_return Success(BlockDeviceCode, BLOCK_DEVICE_OK);
            })
        End

        ResultMatch(BlockCount, BlockDevice__read(self->device, block, 1, entry.data))
            IsSuccess(count,
            {
                if (count != 1)
                {
                    _cs_return Error(BlockDeviceCode, invalid_argument, BLOCK_DEVICE_IO_ERROR);
                }
            })
            IsError(code, message, _cs_return Error(BlockDeviceCode, message, code))
        End

        ResultMatch(BlockDeviceCode, BlockCache__cache_entry(self, entry))
            IsError(code, message, _cs_return Error(BlockDeviceCode, message, code))
        End

        BlockCache__copy(target, entry.data);
        _cs_return Success(BlockDeviceCode, BLOCK_DEVICE_OK);
    });
}
