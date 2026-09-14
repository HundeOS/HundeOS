#ifndef HUND_USER_CHANNEL_BLOCK_CLIENT_H
#define HUND_USER_CHANNEL_BLOCK_CLIENT_H

#include "channel.h"
#include "sys/service.h"

ResultDef(BlockProtocolBlockCount);

typedef struct BlockClient
{
    Channel channel;
    BlockProtocolBlockCount block_count;
    bool open;
} BlockClient;
typedef BlockClient * BlockClientPtr;
ResultDef(BlockClient);

BlockClient BlockClient__new(Channel channel, BlockProtocolBlockCount block_count);
Result(BlockClient) BlockClient__open(ConstBStringPtr device_path);
ChannelCode BlockClient__close(BlockClientPtr self);
Result(BlockProtocolBlockCount)
    BlockClient__read(BlockClientPtr self, BlockProtocolBlockNumber block, BlockProtocolBlockCount block_count,
                      Address buffer, size_t buffer_size);
Result(BlockProtocolBlockCount)
    BlockClient__write(BlockClientPtr self, BlockProtocolBlockNumber block, BlockProtocolBlockCount block_count,
                       Address buffer, size_t buffer_size);

#endif
