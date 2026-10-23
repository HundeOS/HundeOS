#include "native.h"
#include "channel/block_client.h"
#include "channel/block_cache.h"

typedef struct RustBlock
{
    BlockClient client;
    BlockCache cache;
} RustBlock;

typedef RustBlock * RustBlockPtr;

_Static_assert(BLOCK_CHANNEL_SECTOR_SIZE == 512, "Rust sector size must match the block protocol");

static HundeStatusResult RustBlock__error(int32_t code)
{
    return HundeStatusResult__error(code, HundeByteSpan__new((const uint8_t *)"Block operation failed", 22));
}

static RustBlockOpenResult RustBlockOpenResult__new_error(int32_t code)
{
    return (RustBlockOpenResult){.kind = HUNDE_ABI_ERROR,
                                 .error = {.code = code, .message = {(const uint8_t *)"Block open failed", 17}}};
}

static RustBlockOpenResult RustBlockOpenResult__new_success(uint64_t sectors)
{
    return (RustBlockOpenResult){.kind = HUNDE_ABI_SUCCESS, .sector_count = sectors};
}

RustNativeLayout hunde_rust_block_layout(void)
{
    return RustNativeLayout__new(sizeof(RustBlock), _Alignof(RustBlock));
}

RustBlockOpenResult hunde_rust_block_open(void * storage, HundeByteSpan path)
{
    if (path.length == 0 || path.length >= BSTRING_MAX_SIZE)
    {
        return RustBlockOpenResult__new_error(BLOCK_PROTOCOL_INVALID_ARGUMENT);
    }
    BString device = BString__from_chars((const char *)path.bytes, path.length);
    if (BString__contains_char(&device, '\0'))
    {
        return RustBlockOpenResult__new_error(BLOCK_PROTOCOL_INVALID_ARGUMENT);
    }
    match_result(
        BlockClient__open(&device),
        success(client)
        {
            RustBlockPtr target = storage;
            target->client = client;
            BlockCache__init(&target->cache);
            return RustBlockOpenResult__new_success(client.block_count);
        },
        error(code, message)
        {
            return RustBlockOpenResult__new_error(code);
        }
    );
    return RustBlockOpenResult__new_error(RESULT_UNKNOWN_ERROR_CODE);
}

static HundeStatusResult RustBlock__complete(ConstResultPtr(BlockProtocolBlockCount) result, uint64_t expected)
{
    match_result(
        *result,
        success(count)
        {
            if (count != expected)
            {
                return RustBlock__error(BLOCK_PROTOCOL_IO_ERROR);
            }
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustBlock__error(code);
        }
    );
    return RustBlock__error(RESULT_UNKNOWN_ERROR_CODE);
}

HundeStatusResult hunde_rust_block_read(void * storage, uint64_t sector, uint8_t * destination, uint64_t length)
{
    if (length == 0 || length % BLOCK_CHANNEL_SECTOR_SIZE != 0)
    {
        return RustBlock__error(BLOCK_PROTOCOL_INVALID_ARGUMENT);
    }
    uint64_t sectors = length / BLOCK_CHANNEL_SECTOR_SIZE;
    RustBlockPtr self = storage;
    Result(BlockProtocolBlockCount) result =
        BlockCache__read(&self->cache, &self->client, sector, sectors, (Address)destination, length);
    return RustBlock__complete(&result, sectors);
}

HundeStatusResult hunde_rust_block_write(void * storage, uint64_t sector, const uint8_t * source, uint64_t length)
{
    if (length == 0 || length % BLOCK_CHANNEL_SECTOR_SIZE != 0)
    {
        return RustBlock__error(BLOCK_PROTOCOL_INVALID_ARGUMENT);
    }
    uint64_t sectors = length / BLOCK_CHANNEL_SECTOR_SIZE;
    RustBlockPtr self = storage;
    Result(BlockProtocolBlockCount) result =
        BlockCache__write(&self->cache, &self->client, sector, sectors, (Address)source, length);
    return RustBlock__complete(&result, sectors);
}

HundeStatusResult hunde_rust_block_close(void * storage)
{
    RustBlockPtr self = storage;
    BlockCache__reclaim(&self->cache);
    ChannelCode code = BlockClient__close(&self->client);
    if (code != CHANNEL_OK)
    {
        return RustBlock__error(code);
    }
    return HundeStatusResult__success();
}

void hunde_rust_block_reclaim(void * storage)
{
    RustBlockPtr self = storage;
    BlockCache__reclaim(&self->cache);
}
