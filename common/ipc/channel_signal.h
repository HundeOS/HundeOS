#ifndef HUND_CHANNEL_SIGNAL_H
#define HUND_CHANNEL_SIGNAL_H

#include <stdbool.h>
#include <stdint.h>

#define CHANNEL_SIGNAL_CACHE_LINE_SIZE 64

typedef struct ChannelSignalState ChannelSignalState;
typedef ChannelSignalState * ChannelSignalStatePtr;
typedef const ChannelSignalState * ConstChannelSignalStatePtr;

struct ChannelSignalState
{
    uint64_t data_epoch;
    uint64_t space_epoch;
    uint64_t consumer_closed;
    uint64_t producer_closed;
    uint64_t consumer_close_ack;
    uint64_t producer_close_ack;
} __attribute__((aligned(CHANNEL_SIGNAL_CACHE_LINE_SIZE)));

static inline bool ChannelSignalState__consumer_closed(ChannelSignalStatePtr self)
{
    return __atomic_load_n(&self->consumer_closed, __ATOMIC_ACQUIRE) != 0;
}

static inline bool ChannelSignalState__producer_closed(ChannelSignalStatePtr self)
{
    return __atomic_load_n(&self->producer_closed, __ATOMIC_ACQUIRE) != 0;
}

static inline bool ChannelSignalState__closed(ChannelSignalStatePtr self)
{
    return ChannelSignalState__consumer_closed(self) || ChannelSignalState__producer_closed(self);
}

static inline bool ChannelSignalState__consumer_close_acknowledged(ChannelSignalStatePtr self)
{
    return __atomic_load_n(&self->consumer_close_ack, __ATOMIC_ACQUIRE) != 0;
}

static inline bool ChannelSignalState__producer_close_acknowledged(ChannelSignalStatePtr self)
{
    return __atomic_load_n(&self->producer_close_ack, __ATOMIC_ACQUIRE) != 0;
}

static inline void ChannelSignalState__close_consumer(ChannelSignalStatePtr self)
{
    __atomic_store_n(&self->consumer_closed, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->data_epoch, 1, __ATOMIC_RELEASE);
}

static inline void ChannelSignalState__close_producer(ChannelSignalStatePtr self)
{
    __atomic_store_n(&self->producer_closed, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->data_epoch, 1, __ATOMIC_RELEASE);
}

static inline void ChannelSignalState__ack_consumer_close(ChannelSignalStatePtr self)
{
    __atomic_store_n(&self->consumer_close_ack, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->space_epoch, 1, __ATOMIC_RELEASE);
}

static inline void ChannelSignalState__ack_producer_close(ChannelSignalStatePtr self)
{
    __atomic_store_n(&self->producer_close_ack, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->space_epoch, 1, __ATOMIC_RELEASE);
}

#endif
