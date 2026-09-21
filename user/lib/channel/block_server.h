#ifndef HUND_BLOCK_SERVER_H
#define HUND_BLOCK_SERVER_H

#include "server.h"
#include "ipc/block_protocol.h"
#include "types/fixed_array.h"

typedef void * BlockServerContext;
typedef BlockProtocolStatus (*BlockServerTransfer)(BlockServerContext context, BlockProtocolOperation operation,
                                                   BlockProtocolBlockNumber block, BlockProtocolBlockCount count,
                                                   ConstBufferDescriptorPtr buffer);

typedef struct BlockServerConnection
{
    ChannelServer channel;
    BufferPair buffers;
    BlockChannelResponse response;
    bool pending;
    bool authorized;
} BlockServerConnection;
typedef BlockServerConnection * BlockServerConnectionPtr;
OptionalDef(BlockServerConnection);
bool BlockServerConnection__equals(void * first, void * second);
FixedArrayDefine(BlockServerConnection, QUEUE_LIMIT, BlockServerConnections, BlockServerConnection__equals);

typedef struct BlockServer
{
    BString name;
    QueueArrivalsPtr arrivals;
    BlockServerConnections connections;
    BlockProtocolBlockCount block_count;
    BlockServerContext context;
    BlockServerTransfer transfer;
} BlockServer;
typedef BlockServer * BlockServerPtr;
ResultDef(BlockServer);

ChannelCode BlockServer__listen_into(BlockServerPtr self, ConstBStringPtr name, BlockProtocolBlockCount block_count,
                                     BlockServerContext context, BlockServerTransfer transfer);
Result(BlockServer) BlockServer__listen(ConstBStringPtr name, BlockProtocolBlockCount block_count,
                                        BlockServerContext context, BlockServerTransfer transfer);
bool BlockServer__poll(BlockServerPtr self);

#endif
