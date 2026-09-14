#include "block_client.h"

static BString block_protocol_open_failed = bstr("Block channel open failed");
static BString block_protocol_invalid_transfer = bstr("Invalid block transfer");
static BString block_protocol_io_failed = bstr("Block IO failed");

BlockClient BlockClient__new(Channel channel, BlockProtocolBlockCount block_count)
{
    return (BlockClient){.channel = channel, .block_count = block_count, .open = true};
}

static Result(BlockProtocolBlockCount)
    BlockClient__transfer(BlockClientPtr self, BlockProtocolOperation operation, BlockProtocolBlockNumber block,
                          BlockProtocolBlockCount block_count, Address buffer, size_t buffer_size)
{
    if (!self->open || block_count == 0 || block_count > SIZE_MAX / BLOCK_CHANNEL_SECTOR_SIZE ||
        buffer_size != block_count * BLOCK_CHANNEL_SECTOR_SIZE || block > self->block_count ||
        block_count > self->block_count - block)
    {
        return Error(BlockProtocolBlockCount, block_protocol_invalid_transfer, BLOCK_PROTOCOL_INVALID_ARGUMENT);
    }
    for (BlockProtocolBlockCount index = 0; index < block_count; index++)
    {
        BlockChannelRequest request = BlockChannelRequest__new(operation, block + index);
        if (operation == BLOCK_PROTOCOL_WRITE)
        {
            __builtin_memcpy(request.data, (const void *)(buffer + index * BLOCK_CHANNEL_SECTOR_SIZE),
                             sizeof(request.data));
        }
        BlockChannelResponse response;
        match_result(
            Channel__submit(&self->channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&response,
                            sizeof(response)),
            success(size)
            {
                if (size != sizeof(response) || response.status != BLOCK_PROTOCOL_OK || response.block_count != 1)
                {
                    return Error(BlockProtocolBlockCount, block_protocol_io_failed, BLOCK_PROTOCOL_IO_ERROR);
                }
                if (operation == BLOCK_PROTOCOL_READ)
                {
                    __builtin_memcpy((void *)(buffer + index * BLOCK_CHANNEL_SECTOR_SIZE), response.data,
                                     sizeof(response.data));
                }
            },
            error(code, message) { return Error(BlockProtocolBlockCount, message, code); }
        );
    }
    return Success(BlockProtocolBlockCount, block_count);
}

Result(BlockClient) BlockClient__open(ConstBStringPtr device_path)
{
    match_result(
        Channel__open(ServiceDirectory__shared(), device_path),
        success(channel)
        {
            BlockChannelRequest request = BlockChannelRequest__new(BLOCK_PROTOCOL_DESCRIBE, 0);
            BlockChannelResponse response;
            match_result(
                Channel__submit(&channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&response,
                                sizeof(response)),
                success(size)
                {
                    if (size == sizeof(response) && response.status == BLOCK_PROTOCOL_OK && response.block_count > 0)
                    {
                        return Success(BlockClient, BlockClient__new(channel, response.block_count));
                    }
                    Channel__close(&channel);
                    return Error(BlockClient, block_protocol_open_failed, CHANNEL_INVALID_REPLY);
                },
                error(code, message)
                {
                    Channel__close(&channel);
                    return Error(BlockClient, message, code);
                }
            );
        },
        error(code, message) { return Error(BlockClient, message, code); }
    );
    return Error(BlockClient, block_protocol_open_failed, CHANNEL_UNAVAILABLE);
}

ChannelCode BlockClient__close(BlockClientPtr self)
{
    if (!self->open)
    {
        return CHANNEL_OK;
    }
    ChannelCode code = Channel__close(&self->channel);
    self->open = false;
    return code;
}

Result(BlockProtocolBlockCount)
    BlockClient__read(BlockClientPtr self, BlockProtocolBlockNumber block, BlockProtocolBlockCount block_count,
                      Address buffer, size_t buffer_size)
{
    return BlockClient__transfer(self, BLOCK_PROTOCOL_READ, block, block_count, buffer, buffer_size);
}

Result(BlockProtocolBlockCount)
    BlockClient__write(BlockClientPtr self, BlockProtocolBlockNumber block, BlockProtocolBlockCount block_count,
                       Address buffer, size_t buffer_size)
{
    return BlockClient__transfer(self, BLOCK_PROTOCOL_WRITE, block, block_count, buffer, buffer_size);
}
