#include "block_client.h"
#include "buffer/client.h"

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
    BufferDescriptor descriptor = operation == BLOCK_PROTOCOL_READ ? self->buffers.read : self->buffers.write;
    if (descriptor.id == 0)
    {
        return Error(BlockProtocolBlockCount, block_protocol_invalid_transfer, BLOCK_PROTOCOL_INVALID_ARGUMENT);
    }
    for (BlockProtocolBlockCount index = 0; index < block_count;)
    {
        BlockChannelRequest request = BlockChannelRequest__new(operation, block + index);
        request.count = block_count - index;
        if (request.count > BUFFER_SIZE / BLOCK_CHANNEL_SECTOR_SIZE)
        {
            request.count = BUFFER_SIZE / BLOCK_CHANNEL_SECTOR_SIZE;
        }
        request.buffer = descriptor.id;
        size_t bytes = request.count * BLOCK_CHANNEL_SECTOR_SIZE;
        if (operation == BLOCK_PROTOCOL_WRITE)
        {
            __builtin_memcpy((ChannelDataPtr)descriptor.address,
                             (const void *)(buffer + index * BLOCK_CHANNEL_SECTOR_SIZE), bytes);
        }
        BlockChannelResponse response;
        match_result(
            Channel__submit(&self->channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&response,
                            sizeof(response)),
            success(size)
            {
                if (size != sizeof(response) || response.status != BLOCK_PROTOCOL_OK ||
                    response.block_count != request.count)
                {
                    return Error(BlockProtocolBlockCount, block_protocol_io_failed, BLOCK_PROTOCOL_IO_ERROR);
                }
                if (operation == BLOCK_PROTOCOL_READ)
                {
                    __builtin_memcpy((void *)(buffer + index * BLOCK_CHANNEL_SECTOR_SIZE),
                                     (ConstChannelDataPtr)descriptor.address, bytes);
                }
            },
            error(code, message)
            {
                return Error(BlockProtocolBlockCount, message, code);
            }
        );
        index += request.count;
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
                        uint64_t peer = Channel__server_pid(&channel);
                        if (BufferClient__validate(&response.buffers.read, peer, BUFFER_READ) &&
                            BufferClient__validate(&response.buffers.write, peer, BUFFER_WRITE))
                        {
                            BlockClient client = BlockClient__new(channel, response.block_count);
                            client.buffers = response.buffers;
                            return Success(BlockClient, client);
                        }
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
        error(code, message)
        {
            return Error(BlockClient, message, code);
        }
    );
    return Error(BlockClient, block_protocol_open_failed, CHANNEL_UNAVAILABLE);
}

ChannelCode BlockClient__close(BlockClientPtr self)
{
    if (!self->open)
    {
        return CHANNEL_OK;
    }
    BlockChannelRequest request = BlockChannelRequest__new(BLOCK_PROTOCOL_CLOSE, 0);
    BlockChannelResponse response;
    Channel__submit(&self->channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&response,
                    sizeof(response));
    BufferClient__release(&self->buffers.read);
    BufferClient__release(&self->buffers.write);
    self->buffers = BufferPair__new();
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
