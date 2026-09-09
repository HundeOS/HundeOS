#ifndef __COMMON_IPC_CHANNEL_H
#define __COMMON_IPC_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

#define CHANNEL_CACHE_LINE_SIZE 64

typedef struct ChannelState ChannelState;
typedef ChannelState * ChannelStatePtr;
typedef const ChannelState * ConstChannelStatePtr;

struct ChannelState
{
    uint64_t data_epoch;
    uint64_t space_epoch;
    uint64_t consumer_closed;
    uint64_t producer_closed;
    uint64_t consumer_close_ack;
    uint64_t producer_close_ack;
} __attribute__((aligned(CHANNEL_CACHE_LINE_SIZE)));

static inline bool IPCChannelState__consumer_closed(ChannelStatePtr self)
{
    return __atomic_load_n(&self->consumer_closed, __ATOMIC_ACQUIRE) != 0;
}

static inline bool IPCChannelState__producer_closed(ChannelStatePtr self)
{
    return __atomic_load_n(&self->producer_closed, __ATOMIC_ACQUIRE) != 0;
}

static inline bool IPCChannelState__closed(ChannelStatePtr self)
{
    return IPCChannelState__consumer_closed(self) || IPCChannelState__producer_closed(self);
}

static inline bool IPCChannelState__consumer_close_acknowledged(ChannelStatePtr self)
{
    return __atomic_load_n(&self->consumer_close_ack, __ATOMIC_ACQUIRE) != 0;
}

static inline bool IPCChannelState__producer_close_acknowledged(ChannelStatePtr self)
{
    return __atomic_load_n(&self->producer_close_ack, __ATOMIC_ACQUIRE) != 0;
}

static inline void IPCChannelState__close_consumer(ChannelStatePtr self)
{
    __atomic_store_n(&self->consumer_closed, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->data_epoch, 1, __ATOMIC_RELEASE);
}

static inline void IPCChannelState__close_producer(ChannelStatePtr self)
{
    __atomic_store_n(&self->producer_closed, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->data_epoch, 1, __ATOMIC_RELEASE);
}

static inline void IPCChannelState__ack_consumer_close(ChannelStatePtr self)
{
    __atomic_store_n(&self->consumer_close_ack, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->space_epoch, 1, __ATOMIC_RELEASE);
}

static inline void IPCChannelState__ack_producer_close(ChannelStatePtr self)
{
    __atomic_store_n(&self->producer_close_ack, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->space_epoch, 1, __ATOMIC_RELEASE);
}

#endif
