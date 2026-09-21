#include "channel/block_runtime.h"
#include "sys/service.h"

#define CHANNEL_BLK_BLOCK_SIZE BLOCK_CHANNEL_SECTOR_SIZE
#define CHANNEL_BLK_BLOCK_COUNT 8ULL

typedef struct ChannelBlk
{
    uint8_t storage[CHANNEL_BLK_BLOCK_SIZE * CHANNEL_BLK_BLOCK_COUNT];
} ChannelBlk;
typedef ChannelBlk * ChannelBlkPtr;

static ChannelBlk ChannelBlk__new(void)
{
    return (ChannelBlk){0};
}

static BlockProtocolStatus ChannelBlk__transfer(BlockServerContext context, BlockProtocolOperation operation,
                                                BlockProtocolBlockNumber block, BlockProtocolBlockCount count,
                                                ConstBufferDescriptorPtr buffer)
{
    ChannelBlkPtr self = context;
    if (count == 0 || block >= CHANNEL_BLK_BLOCK_COUNT || count > CHANNEL_BLK_BLOCK_COUNT - block ||
        count > buffer->capacity / CHANNEL_BLK_BLOCK_SIZE)
    {
        return BLOCK_PROTOCOL_INVALID_ARGUMENT;
    }
    size_t bytes = count * CHANNEL_BLK_BLOCK_SIZE;
    size_t offset = block * CHANNEL_BLK_BLOCK_SIZE;
    if (operation == BLOCK_PROTOCOL_READ)
    {
        __builtin_memcpy((void *)buffer->address, self->storage + offset, bytes);
    }
    else if (operation == BLOCK_PROTOCOL_WRITE)
    {
        __builtin_memcpy(self->storage + offset, (void *)buffer->address, bytes);
    }
    else
    {
        return BLOCK_PROTOCOL_INVALID_ARGUMENT;
    }
    return BLOCK_PROTOCOL_OK;
}

int main(int argc, char ** argv)
{
    if (argc != 2 || !service_ok(service_become_service()))
    {
        return 1;
    }
    BString path = BString__new(argv[1]);
    ChannelBlk device = ChannelBlk__new();
    if (!service_ok(service_register_block_device(&path, CHANNEL_BLK_BLOCK_SIZE, CHANNEL_BLK_BLOCK_COUNT)))
    {
        return 1;
    }
    BlockServer server;
    if (BlockServer__listen_into(&server, &path, CHANNEL_BLK_BLOCK_COUNT, &device, ChannelBlk__transfer) != CHANNEL_OK)
    {
        return 1;
    }
    if (!service_ok(service_publish_ready(&path)))
    {
        return 1;
    }
    return BlockServer__run(&server);
}
