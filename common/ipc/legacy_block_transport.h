#ifndef HUND_LEGACY_BLOCK_TRANSPORT_H
#define HUND_LEGACY_BLOCK_TRANSPORT_H

// Compatibility for the old kernel block-connect syscall and its remaining callers.
#include "block_protocol.h"
#include "channel_signal.h"
#define BLOCK_PROTOCOL_REQUEST_CAPACITY 16
#define BLOCK_PROTOCOL_RESPONSE_CAPACITY 16
#define BLOCK_PROTOCOL_DATA_SIZE 4096

typedef struct BlockProtocolRequest
{
    BlockProtocolRequestID request_id;
    BlockProtocolOperation operation;
    BlockProtocolBlockNumber block;
    BlockProtocolBlockCount block_count;
    BlockProtocolDataOffset data_offset;
    BlockProtocolByteCount byte_count;
} BlockProtocolRequest;

typedef BlockProtocolRequest * BlockProtocolRequestPtr;

static inline BlockProtocolRequest
BlockProtocolRequest__new(BlockProtocolRequestID request_id, BlockProtocolOperation operation,
                          BlockProtocolBlockNumber block, BlockProtocolBlockCount block_count,
                          BlockProtocolDataOffset data_offset, BlockProtocolByteCount byte_count)
{
    return (BlockProtocolRequest){
        .request_id = request_id,
        .operation = operation,
        .block = block,
        .block_count = block_count,
        .data_offset = data_offset,
        .byte_count = byte_count,
    };
}

typedef struct BlockProtocolResponse
{
    BlockProtocolRequestID request_id;
    BlockProtocolStatus status;
    BlockProtocolBlockCount block_count;
    BlockProtocolDataOffset data_offset;
    BlockProtocolByteCount byte_count;
} BlockProtocolResponse;

typedef BlockProtocolResponse * BlockProtocolResponsePtr;

static inline BlockProtocolResponse BlockProtocolResponse__new(BlockProtocolRequestID request_id,
                                                               BlockProtocolStatus status,
                                                               BlockProtocolBlockCount block_count,
                                                               BlockProtocolDataOffset data_offset,
                                                               BlockProtocolByteCount byte_count)
{
    return (BlockProtocolResponse){
        .request_id = request_id,
        .status = status,
        .block_count = block_count,
        .data_offset = data_offset,
        .byte_count = byte_count,
    };
}

typedef struct BlockProtocolRequestRing
{
    uint64_t read_index;
    uint64_t write_index;
    BlockProtocolRequest entries[BLOCK_PROTOCOL_REQUEST_CAPACITY];
} BlockProtocolRequestRing;

typedef BlockProtocolRequestRing * BlockProtocolRequestRingPtr;

typedef struct BlockProtocolResponseRing
{
    uint64_t read_index;
    uint64_t write_index;
    BlockProtocolResponse entries[BLOCK_PROTOCOL_RESPONSE_CAPACITY];
} BlockProtocolResponseRing;

typedef BlockProtocolResponseRing * BlockProtocolResponseRingPtr;

typedef struct BlockProtocolSignalState
{
    ChannelSignalState channel;
    uint64_t next_request_id;
} __attribute__((aligned(CHANNEL_SIGNAL_CACHE_LINE_SIZE))) BlockProtocolSignalState;

typedef BlockProtocolSignalState * BlockProtocolSignalStatePtr;

typedef struct BlockProtocolMemory
{
    BlockProtocolSignalState state;
    BlockProtocolRequestRing requests;
    BlockProtocolResponseRing responses;
    uint8_t data[BLOCK_PROTOCOL_DATA_SIZE];
} BlockProtocolMemory;

typedef BlockProtocolMemory * BlockProtocolMemoryPtr;

static inline bool BlockProtocolSignalState__closed(BlockProtocolSignalStatePtr self)
{
    return ChannelSignalState__closed(&self->channel);
}

static inline bool BlockProtocolSignalState__consumer_closed(BlockProtocolSignalStatePtr self)
{
    return ChannelSignalState__consumer_closed(&self->channel);
}

static inline bool BlockProtocolSignalState__producer_closed(BlockProtocolSignalStatePtr self)
{
    return ChannelSignalState__producer_closed(&self->channel);
}

static inline void BlockProtocolSignalState__close_consumer(BlockProtocolSignalStatePtr self)
{
    ChannelSignalState__close_consumer(&self->channel);
}

static inline void BlockProtocolSignalState__close_producer(BlockProtocolSignalStatePtr self)
{
    ChannelSignalState__close_producer(&self->channel);
}

static inline void BlockProtocolSignalState__ack_consumer_close(BlockProtocolSignalStatePtr self)
{
    ChannelSignalState__ack_consumer_close(&self->channel);
}

static inline void BlockProtocolSignalState__ack_producer_close(BlockProtocolSignalStatePtr self)
{
    ChannelSignalState__ack_producer_close(&self->channel);
}

static inline uint64_t BlockProtocolSignalState__next_request_id(BlockProtocolSignalStatePtr self)
{
    return __atomic_add_fetch(&self->next_request_id, 1, __ATOMIC_RELEASE);
}

static inline void BlockProtocolMemory__init(BlockProtocolMemoryPtr self)
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

static inline ChannelSignalStatePtr BlockProtocolMemory__channel_state(BlockProtocolMemoryPtr self)
{
    return &self->state.channel;
}

static inline bool BlockProtocolMemory__closed(BlockProtocolMemoryPtr self)
{
    return BlockProtocolSignalState__closed(&self->state);
}

static inline bool BlockProtocolMemory__producer_closed(BlockProtocolMemoryPtr self)
{
    return BlockProtocolSignalState__producer_closed(&self->state);
}

static inline bool BlockProtocolMemory__consumer_closed(BlockProtocolMemoryPtr self)
{
    return BlockProtocolSignalState__consumer_closed(&self->state);
}

static inline void BlockProtocolMemory__ack_producer_close(BlockProtocolMemoryPtr self)
{
    BlockProtocolSignalState__ack_producer_close(&self->state);
}

static inline void BlockProtocolMemory__ack_consumer_close(BlockProtocolMemoryPtr self)
{
    BlockProtocolSignalState__ack_consumer_close(&self->state);
}

static inline uint64_t BlockProtocolMemory__next_request_id(BlockProtocolMemoryPtr self)
{
    return BlockProtocolSignalState__next_request_id(&self->state);
}

static inline BlockProtocolStatus BlockProtocolMemory__write_data(BlockProtocolMemoryPtr self, uint64_t offset,
                                                                  const void * source, size_t size)
{
    if (offset > BLOCK_PROTOCOL_DATA_SIZE || size > BLOCK_PROTOCOL_DATA_SIZE - offset)
    {
        return BLOCK_PROTOCOL_INVALID_ARGUMENT;
    }
    __builtin_memcpy(self->data + offset, source, size);
    return BLOCK_PROTOCOL_OK;
}

static inline BlockProtocolStatus BlockProtocolMemory__read_data(BlockProtocolMemoryPtr self, uint64_t offset,
                                                                 void * target, size_t size)
{
    if (offset > BLOCK_PROTOCOL_DATA_SIZE || size > BLOCK_PROTOCOL_DATA_SIZE - offset)
    {
        return BLOCK_PROTOCOL_INVALID_ARGUMENT;
    }
    __builtin_memcpy(target, self->data + offset, size);
    return BLOCK_PROTOCOL_OK;
}

static inline void * BlockProtocolMemory__data_at(BlockProtocolMemoryPtr self, uint64_t offset)
{
    return self->data + offset;
}

static inline bool BlockProtocolRequestRing__full(BlockProtocolRequestRingPtr self)
{
    return self->write_index - self->read_index >= BLOCK_PROTOCOL_REQUEST_CAPACITY;
}

static inline bool BlockProtocolRequestRing__empty(BlockProtocolRequestRingPtr self)
{
    return self->write_index == self->read_index;
}

static inline BlockProtocolStatus BlockProtocolRequestRing__push(BlockProtocolRequestRingPtr self,
                                                                 BlockProtocolSignalStatePtr state,
                                                                 BlockProtocolRequestPtr request)
{
    if (BlockProtocolSignalState__closed(state))
    {
        return BLOCK_PROTOCOL_CLOSED;
    }
    if (BlockProtocolRequestRing__full(self))
    {
        return BLOCK_PROTOCOL_FULL;
    }

    self->entries[self->write_index % BLOCK_PROTOCOL_REQUEST_CAPACITY] = *request;
    __atomic_add_fetch(&self->write_index, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&state->channel.data_epoch, 1, __ATOMIC_RELEASE);
    return BLOCK_PROTOCOL_OK;
}

static inline BlockProtocolStatus BlockProtocolRequestRing__pop(BlockProtocolRequestRingPtr self,
                                                                BlockProtocolSignalStatePtr state,
                                                                BlockProtocolRequestPtr request)
{
    if (BlockProtocolRequestRing__empty(self))
    {
        return BlockProtocolSignalState__closed(state) ? BLOCK_PROTOCOL_CLOSED : BLOCK_PROTOCOL_EMPTY;
    }

    *request = self->entries[self->read_index % BLOCK_PROTOCOL_REQUEST_CAPACITY];
    __atomic_add_fetch(&self->read_index, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&state->channel.space_epoch, 1, __ATOMIC_RELEASE);
    return BLOCK_PROTOCOL_OK;
}

static inline bool BlockProtocolResponseRing__full(BlockProtocolResponseRingPtr self)
{
    return self->write_index - self->read_index >= BLOCK_PROTOCOL_RESPONSE_CAPACITY;
}

static inline bool BlockProtocolResponseRing__empty(BlockProtocolResponseRingPtr self)
{
    return self->write_index == self->read_index;
}

static inline BlockProtocolStatus BlockProtocolResponseRing__push(BlockProtocolResponseRingPtr self,
                                                                  BlockProtocolSignalStatePtr state,
                                                                  BlockProtocolResponsePtr response)
{
    if (BlockProtocolSignalState__closed(state))
    {
        return BLOCK_PROTOCOL_CLOSED;
    }
    if (BlockProtocolResponseRing__full(self))
    {
        return BLOCK_PROTOCOL_FULL;
    }

    self->entries[self->write_index % BLOCK_PROTOCOL_RESPONSE_CAPACITY] = *response;
    __atomic_add_fetch(&self->write_index, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&state->channel.data_epoch, 1, __ATOMIC_RELEASE);
    return BLOCK_PROTOCOL_OK;
}

static inline BlockProtocolStatus BlockProtocolResponseRing__pop(BlockProtocolResponseRingPtr self,
                                                                 BlockProtocolSignalStatePtr state,
                                                                 BlockProtocolResponsePtr response)
{
    if (BlockProtocolResponseRing__empty(self))
    {
        return BlockProtocolSignalState__closed(state) ? BLOCK_PROTOCOL_CLOSED : BLOCK_PROTOCOL_EMPTY;
    }

    *response = self->entries[self->read_index % BLOCK_PROTOCOL_RESPONSE_CAPACITY];
    __atomic_add_fetch(&self->read_index, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&state->channel.space_epoch, 1, __ATOMIC_RELEASE);
    return BLOCK_PROTOCOL_OK;
}

static inline bool BlockProtocolMemory__requests_empty(BlockProtocolMemoryPtr self)
{
    return BlockProtocolRequestRing__empty(&self->requests);
}

static inline BlockProtocolStatus BlockProtocolMemory__push_request(BlockProtocolMemoryPtr self,
                                                                    BlockProtocolRequestPtr request)
{
    return BlockProtocolRequestRing__push(&self->requests, &self->state, request);
}

static inline BlockProtocolStatus BlockProtocolMemory__pop_request(BlockProtocolMemoryPtr self,
                                                                   BlockProtocolRequestPtr request)
{
    return BlockProtocolRequestRing__pop(&self->requests, &self->state, request);
}

static inline BlockProtocolStatus BlockProtocolMemory__push_response(BlockProtocolMemoryPtr self,
                                                                     BlockProtocolResponsePtr response)
{
    return BlockProtocolResponseRing__push(&self->responses, &self->state, response);
}

static inline BlockProtocolStatus BlockProtocolMemory__pop_response(BlockProtocolMemoryPtr self,
                                                                    BlockProtocolResponsePtr response)
{
    return BlockProtocolResponseRing__pop(&self->responses, &self->state, response);
}

#endif
