#ifndef __COMMON_IPC_BLOCK_PROTOCOL_H
#define __COMMON_IPC_BLOCK_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t BlockConnectionID;
typedef uint64_t BlockProtocolRequestID;
typedef uint64_t BlockProtocolBlockNumber;
typedef uint64_t BlockProtocolBlockCount;
typedef uint64_t BlockProtocolDataOffset;
typedef size_t BlockProtocolByteCount;

typedef enum BlockProtocolOperation
{
    BLOCK_PROTOCOL_READ = 1,
    BLOCK_PROTOCOL_WRITE = 2,
    BLOCK_PROTOCOL_DESCRIBE = 3,
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

// Sector transfers fit in the generic channel payload. Larger IO is split by the client.
#define BLOCK_CHANNEL_SECTOR_SIZE 512U
typedef struct BlockChannelRequest
{
    BlockProtocolOperation operation;
    BlockProtocolBlockNumber block;
    uint8_t data[BLOCK_CHANNEL_SECTOR_SIZE];
} BlockChannelRequest;
typedef struct BlockChannelResponse
{
    BlockProtocolStatus status;
    BlockProtocolBlockCount block_count;
    uint8_t data[BLOCK_CHANNEL_SECTOR_SIZE];
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
