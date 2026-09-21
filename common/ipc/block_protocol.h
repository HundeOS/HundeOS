#ifndef __COMMON_IPC_BLOCK_PROTOCOL_H
#define __COMMON_IPC_BLOCK_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "buffer.h"

typedef uint64_t BlockProtocolBlockNumber;
typedef uint64_t BlockProtocolBlockCount;

typedef enum BlockProtocolOperation
{
    BLOCK_PROTOCOL_READ = 1,
    BLOCK_PROTOCOL_WRITE = 2,
    BLOCK_PROTOCOL_DESCRIBE = 3,
    BLOCK_PROTOCOL_CLOSE = 4,
} BlockProtocolOperation;

typedef enum BlockProtocolStatus
{
    BLOCK_PROTOCOL_OK = 0,
    BLOCK_PROTOCOL_FULL = -1,
    BLOCK_PROTOCOL_EMPTY = -2,
    BLOCK_PROTOCOL_CLOSED = -3,
    BLOCK_PROTOCOL_INVALID_ARGUMENT = -4,
    BLOCK_PROTOCOL_IO_ERROR = -5,
} BlockProtocolStatus;

// Channels carry transfer descriptors; sector bytes live in shared buffers.
#define BLOCK_CHANNEL_SECTOR_SIZE 512U
typedef struct BlockChannelRequest
{
    BlockProtocolOperation operation;
    BlockProtocolBlockNumber block;
    BlockProtocolBlockCount count;
    BufferID buffer;
} BlockChannelRequest;
typedef BlockChannelRequest * BlockChannelRequestPtr;
typedef struct BlockChannelResponse
{
    BlockProtocolStatus status;
    BlockProtocolBlockCount block_count;
    BufferPair buffers;
} BlockChannelResponse;

static inline BlockChannelRequest BlockChannelRequest__new(BlockProtocolOperation operation,
                                                           BlockProtocolBlockNumber block)
{
    return (BlockChannelRequest){.operation = operation, .block = block};
}

static inline BlockChannelResponse BlockChannelResponse__new(BlockProtocolStatus status,
                                                             BlockProtocolBlockCount block_count)
{
    return (BlockChannelResponse){.status = status, .block_count = block_count};
}

#endif
