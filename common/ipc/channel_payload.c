#include "channel_protocol.h"

static bool ChannelPayload__copy(ChannelDataPtr target, ConstChannelDataPtr source, size_t size)
{
    if (size > CHANNEL_PAYLOAD_CAPACITY)
    {
        return false;
    }
    for (size_t index = 0; index < size; index++)
    {
        target[index] = source[index];
    }
    return true;
}

bool ChannelPayload__write_request(ChannelPayloadPtr self, ConstChannelDataPtr data, size_t size)
{
    return ChannelPayload__copy(self->request, data, size);
}

bool ChannelPayload__read_request(ChannelPayloadPtr self, ChannelDataPtr data, size_t size)
{
    return ChannelPayload__copy(data, self->request, size);
}

bool ChannelPayload__write_reply(ChannelPayloadPtr self, ConstChannelDataPtr data, size_t size)
{
    return ChannelPayload__copy(self->reply, data, size);
}

bool ChannelPayload__read_reply(ChannelPayloadPtr self, ChannelDataPtr data, size_t size)
{
    return ChannelPayload__copy(data, self->reply, size);
}

void ChannelPayload__begin_request(ChannelPayloadPtr self, uint64_t published_at_usec)
{
    self->request_published_at_usec = published_at_usec;
}

uint64_t ChannelPayload__request_published_at(ChannelPayloadPtr self)
{
    return self->request_published_at_usec;
}
