#ifndef __COMMON_IPC_ECHO_CHANNEL_H
#define __COMMON_IPC_ECHO_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

#include "channel.h"
#include "../types/bstring.h"

#define ECHO_CHANNEL_DATA_SIZE 256U

typedef enum EchoChannelStatus
{
    ECHO_CHANNEL_OK = 0,
    ECHO_CHANNEL_EMPTY = 1,
    ECHO_CHANNEL_FULL = 2,
    ECHO_CHANNEL_TOO_LARGE = 3,
    ECHO_CHANNEL_CLOSED = 4,
} EchoChannelStatus;

typedef struct EchoChannelMessage
{
    uint64_t request_id;
    size_t size;
    uint8_t data[ECHO_CHANNEL_DATA_SIZE];
} EchoChannelMessage;

typedef EchoChannelMessage * EchoChannelMessagePtr;

typedef struct EchoChannelMemory
{
    ChannelState state;
    uint64_t request_ready;
    uint64_t response_ready;
    EchoChannelMessage request;
    EchoChannelMessage response;
} EchoChannelMemory;

typedef EchoChannelMemory * EchoChannelMemoryPtr;

static inline EchoChannelMessage EchoChannelMessage__new(void)
{
    return (EchoChannelMessage){
        .request_id = 0,
        .size = 0,
    };
}

static inline void EchoChannelMemory__init(EchoChannelMemoryPtr self)
{
    self->state.data_epoch = 0;
    self->state.space_epoch = 0;
    self->state.consumer_closed = 0;
    self->state.producer_closed = 0;
    self->state.consumer_close_ack = 0;
    self->state.producer_close_ack = 0;
    self->request_ready = 0;
    self->response_ready = 0;
    self->request = EchoChannelMessage__new();
    self->response = EchoChannelMessage__new();
}

static inline bool EchoChannelMemory__request_ready(EchoChannelMemoryPtr self)
{
    return __atomic_load_n(&self->request_ready, __ATOMIC_ACQUIRE) != 0;
}

static inline bool EchoChannelMemory__response_ready(EchoChannelMemoryPtr self)
{
    return __atomic_load_n(&self->response_ready, __ATOMIC_ACQUIRE) != 0;
}

static inline EchoChannelStatus EchoChannelMessage__copy_from_bstring(EchoChannelMessagePtr self, uint64_t request_id,
                                                                      ConstBStringPtr message)
{
    size_t size = (size_t)BString__length(message);
    if (size > ECHO_CHANNEL_DATA_SIZE)
    {
        return ECHO_CHANNEL_TOO_LARGE;
    }

    self->request_id = request_id;
    self->size = size;
    for (size_t i = 0; i < size; i++)
    {
        self->data[i] = (uint8_t)BString__char_at_or(message, (StringPos)i, '\0');
    }

    return ECHO_CHANNEL_OK;
}

static inline EchoChannelStatus EchoChannelMessage__copy_to_bstring(EchoChannelMessagePtr self, BStringPtr target)
{
    BString__clear(target);
    if (self->size > ECHO_CHANNEL_DATA_SIZE)
    {
        return ECHO_CHANNEL_TOO_LARGE;
    }

    for (size_t i = 0; i < self->size; i++)
    {
        if (!BString__push_char(target, (char)self->data[i]))
        {
            return ECHO_CHANNEL_TOO_LARGE;
        }
    }

    return ECHO_CHANNEL_OK;
}

static inline EchoChannelStatus EchoChannelMemory__submit(EchoChannelMemoryPtr self, EchoChannelMessagePtr request)
{
    if (IPCChannelState__closed(&self->state))
    {
        return ECHO_CHANNEL_CLOSED;
    }
    if (EchoChannelMemory__request_ready(self))
    {
        return ECHO_CHANNEL_FULL;
    }

    self->request = *request;
    __atomic_store_n(&self->request_ready, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->state.data_epoch, 1, __ATOMIC_RELEASE);
    return ECHO_CHANNEL_OK;
}

static inline EchoChannelStatus EchoChannelMemory__take_request(EchoChannelMemoryPtr self,
                                                                EchoChannelMessagePtr request)
{
    if (!EchoChannelMemory__request_ready(self))
    {
        return IPCChannelState__closed(&self->state) ? ECHO_CHANNEL_CLOSED : ECHO_CHANNEL_EMPTY;
    }

    *request = self->request;
    __atomic_store_n(&self->request_ready, 0, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->state.space_epoch, 1, __ATOMIC_RELEASE);
    return ECHO_CHANNEL_OK;
}

static inline EchoChannelStatus EchoChannelMemory__respond(EchoChannelMemoryPtr self, EchoChannelMessagePtr response)
{
    if (IPCChannelState__closed(&self->state))
    {
        return ECHO_CHANNEL_CLOSED;
    }
    if (EchoChannelMemory__response_ready(self))
    {
        return ECHO_CHANNEL_FULL;
    }

    self->response = *response;
    __atomic_store_n(&self->response_ready, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->state.data_epoch, 1, __ATOMIC_RELEASE);
    return ECHO_CHANNEL_OK;
}

static inline EchoChannelStatus EchoChannelMemory__take_response(EchoChannelMemoryPtr self,
                                                                 EchoChannelMessagePtr response)
{
    if (!EchoChannelMemory__response_ready(self))
    {
        return IPCChannelState__closed(&self->state) ? ECHO_CHANNEL_CLOSED : ECHO_CHANNEL_EMPTY;
    }

    *response = self->response;
    __atomic_store_n(&self->response_ready, 0, __ATOMIC_RELEASE);
    __atomic_add_fetch(&self->state.space_epoch, 1, __ATOMIC_RELEASE);
    return ECHO_CHANNEL_OK;
}

#endif
