#ifndef __COMMON_IPC_BLOCK_CHANNEL_H
#define __COMMON_IPC_BLOCK_CHANNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "channel.h"

#define BLOCK_CHANNEL_REQUEST_CAPACITY 16
#define BLOCK_CHANNEL_RESPONSE_CAPACITY 16
#define BLOCK_CHANNEL_DATA_SIZE 4096

typedef enum BlockChannelOperation
{
    BLOCK_CHANNEL_READ = 1,
    BLOCK_CHANNEL_WRITE = 2,
} BlockChannelOperation;

typedef enum BlockChannelStatus
{
    BLOCK_CHANNEL_OK = 0,
    BLOCK_CHANNEL_FULL = -1,
    BLOCK_CHANNEL_EMPTY = -2,
    BLOCK_CHANNEL_CLOSED = -3,
    BLOCK_CHANNEL_INVALID_ARGUMENT = -4,
} BlockChannelStatus;

typedef struct BlockChannelRequest
{
    uint64_t request_id;
    BlockChannelOperation operation;
    uint64_t block;
    uint64_t block_count;
    uint64_t data_offset;
    size_t byte_count;
} BlockChannelRequest;

typedef BlockChannelRequest * BlockChannelRequestPtr;

static inline BlockChannelRequest BlockChannelRequest__new(uint64_t request_id, BlockChannelOperation operation,
                                                           uint64_t block, uint64_t block_count, uint64_t data_offset,
                                                           size_t byte_count)
{
    return (BlockChannelRequest){
        .request_id = request_id,
        .operation = operation,
        .block = block,
        .block_count = block_count,
        .data_offset = data_offset,
        .byte_count = byte_count,
    };
}

typedef struct BlockChannelResponse
{
    uint64_t request_id;
    BlockChannelStatus status;
    uint64_t block_count;
    uint64_t data_offset;
    size_t byte_count;
} BlockChannelResponse;

typedef BlockChannelResponse * BlockChannelResponsePtr;

static inline BlockChannelResponse BlockChannelResponse__new(uint64_t request_id, BlockChannelStatus status,
                                                             uint64_t block_count, uint64_t data_offset,
                                                             size_t byte_count)
{
    return (BlockChannelResponse){
        .request_id = request_id,
        .status = status,
        .block_count = block_count,
        .data_offset = data_offset,
        .byte_count = byte_count,
    };
}

typedef struct BlockChannelRequestRing
{
    uint64_t read_index;
    uint64_t write_index;
    BlockChannelRequest entries[BLOCK_CHANNEL_REQUEST_CAPACITY];
} BlockChannelRequestRing;

typedef BlockChannelRequestRing * BlockChannelRequestRingPtr;

typedef struct BlockChannelResponseRing
{
    uint64_t read_index;
    uint64_t write_index;
    BlockChannelResponse entries[BLOCK_CHANNEL_RESPONSE_CAPACITY];
} BlockChannelResponseRing;

typedef BlockChannelResponseRing * BlockChannelResponseRingPtr;

typedef struct BlockChannelState
{
    ChannelState channel;
    uint64_t next_request_id;
} __attribute__((aligned(CHANNEL_CACHE_LINE_SIZE))) BlockChannelState;

typedef BlockChannelState * BlockChannelStatePtr;

typedef struct BlockChannelMemory
{
    BlockChannelState state;
    BlockChannelRequestRing requests;
    BlockChannelResponseRing responses;
    uint8_t data[BLOCK_CHANNEL_DATA_SIZE];
} BlockChannelMemory;

typedef BlockChannelMemory * BlockChannelMemoryPtr;

static inline bool BlockChannelState__closed(BlockChannelStatePtr self)
{
    return IPCChannelState__closed(&self->channel);
}

static inline bool BlockChannelState__consumer_closed(BlockChannelStatePtr self)
{
    return IPCChannelState__consumer_closed(&self->channel);
}

static inline bool BlockChannelState__producer_closed(BlockChannelStatePtr self)
{
    return IPCChannelState__producer_closed(&self->channel);
}

static inline void BlockChannelState__close_consumer(BlockChannelStatePtr self)
{
    IPCChannelState__close_consumer(&self->channel);
}

static inline void BlockChannelState__close_producer(BlockChannelStatePtr self)
{
    IPCChannelState__close_producer(&self->channel);
}

static inline void BlockChannelState__ack_consumer_close(BlockChannelStatePtr self)
{
    IPCChannelState__ack_consumer_close(&self->channel);
}

static inline void BlockChannelState__ack_producer_close(BlockChannelStatePtr self)
{
    IPCChannelState__ack_producer_close(&self->channel);
}

static inline uint64_t BlockChannelState__next_request_id(BlockChannelStatePtr self)
{
    return __atomic_add_fetch(&self->next_request_id, 1, __ATOMIC_RELEASE);
}

static inline void BlockChannelMemory__init(BlockChannelMemoryPtr self)
{
    self->state.channel.data_epoch = 0;
    self->state.channel.space_epoch = 0;
    self->state.channel.consumer_closed = 0;
    self->state.channel.producer_closed = 0;
    self->state.channel.consumer_close_ack = 0;
    self->state.channel.producer_close_ack = 0;
    self->state.next_request_id = 0;
    self->requests.read_index = 0;
    self->requests.write_index = 0;
    self->responses.read_index = 0;
    self->responses.write_index = 0;
}

static inline bool BlockChannelRequestRing__full(BlockChannelRequestRingPtr self)
{
    return self->write_index - self->read_index >= BLOCK_CHANNEL_REQUEST_CAPACITY;
}

static inline bool BlockChannelRequestRing__empty(BlockChannelRequestRingPtr self)
{
    return self->write_index == self->read_index;
}

static inline BlockChannelStatus BlockChannelRequestRing__push(BlockChannelRequestRingPtr self,
                                                               BlockChannelStatePtr state,
                                                               BlockChannelRequestPtr request)
{
    if (BlockChannelState__closed(state))
    {
        return BLOCK_CHANNEL_CLOSED;
    }
    if (BlockChannelRequestRing__full(self))
    {
        return BLOCK_CHANNEL_FULL;
    }

    self->entries[self->write_index % BLOCK_CHANNEL_REQUEST_CAPACITY] = *request;
    __atomic_add_fetch(&self->write_index, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&state->channel.data_epoch, 1, __ATOMIC_RELEASE);
    return BLOCK_CHANNEL_OK;
}

static inline BlockChannelStatus BlockChannelRequestRing__pop(BlockChannelRequestRingPtr self,
                                                              BlockChannelStatePtr state,
                                                              BlockChannelRequestPtr request)
{
    if (BlockChannelRequestRing__empty(self))
    {
        return BlockChannelState__closed(state) ? BLOCK_CHANNEL_CLOSED : BLOCK_CHANNEL_EMPTY;
    }

    *request = self->entries[self->read_index % BLOCK_CHANNEL_REQUEST_CAPACITY];
    __atomic_add_fetch(&self->read_index, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&state->channel.space_epoch, 1, __ATOMIC_RELEASE);
    return BLOCK_CHANNEL_OK;
}

static inline bool BlockChannelResponseRing__full(BlockChannelResponseRingPtr self)
{
    return self->write_index - self->read_index >= BLOCK_CHANNEL_RESPONSE_CAPACITY;
}

static inline bool BlockChannelResponseRing__empty(BlockChannelResponseRingPtr self)
{
    return self->write_index == self->read_index;
}

static inline BlockChannelStatus BlockChannelResponseRing__push(BlockChannelResponseRingPtr self,
                                                                BlockChannelStatePtr state,
                                                                BlockChannelResponsePtr response)
{
    if (BlockChannelState__closed(state))
    {
        return BLOCK_CHANNEL_CLOSED;
    }
    if (BlockChannelResponseRing__full(self))
    {
        return BLOCK_CHANNEL_FULL;
    }

    self->entries[self->write_index % BLOCK_CHANNEL_RESPONSE_CAPACITY] = *response;
    __atomic_add_fetch(&self->write_index, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&state->channel.data_epoch, 1, __ATOMIC_RELEASE);
    return BLOCK_CHANNEL_OK;
}

static inline BlockChannelStatus BlockChannelResponseRing__pop(BlockChannelResponseRingPtr self,
                                                               BlockChannelStatePtr state,
                                                               BlockChannelResponsePtr response)
{
    if (BlockChannelResponseRing__empty(self))
    {
        return BlockChannelState__closed(state) ? BLOCK_CHANNEL_CLOSED : BLOCK_CHANNEL_EMPTY;
    }

    *response = self->entries[self->read_index % BLOCK_CHANNEL_RESPONSE_CAPACITY];
    __atomic_add_fetch(&self->read_index, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&state->channel.space_epoch, 1, __ATOMIC_RELEASE);
    return BLOCK_CHANNEL_OK;
}

#endif
