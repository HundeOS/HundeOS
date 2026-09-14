#include "channel.h"

static BString channel_transport_failed = bstr("Channel transport failed");
static BString channel_invalid_reply = bstr("Invalid channel reply");
static BString channel_payload_too_large = bstr("Channel payload too large");

Channel Channel__new(QueuePtr control, ChannelPayloadPtr payload)
{
    return (Channel){
        .control = *control,
        .payload = payload,
        .token = Queue__connection(control),
        .request_pending = false,
        .received_reply = None(ChannelReplySize),
    };
}

Result(ChannelCode) Channel__try_request(ChannelPtr self, ConstChannelDataPtr request, size_t request_size)
{
    if (self->request_pending)
    {
        return Error(ChannelCode, channel_invalid_reply, CHANNEL_BUSY);
    }
    if (request_size >= CHANNEL_PAYLOAD_CAPACITY ||
        !ChannelPayload__write_request(self->payload, request, request_size))
    {
        return Error(ChannelCode, channel_payload_too_large, CHANNEL_PAYLOAD_TOO_LARGE);
    }
    QueueMessage submit = ChannelControlMessage__new(CHANNEL_REQUEST_SUBMIT, 0, request_size, self->token);
    match_result(
        Queue__push(&self->control, &submit),
        success(code)
        {
            self->request_pending = true;
            return Success(ChannelCode, CHANNEL_OK);
        },
        error(code, message)
        {
            if (code == QUEUE_FULL || code == QUEUE_PENDING)
            {
                return Success(ChannelCode, CHANNEL_PENDING);
            }
            return Error(ChannelCode, channel_transport_failed, code);
        }
    );
    return Error(ChannelCode, channel_transport_failed, CHANNEL_TRANSPORT_FAILED);
}

static Result(OptionalChannelReplySize)
    Channel__acknowledge(ChannelPtr self, ChannelDataPtr reply, size_t reply_capacity, ChannelReplySize size)
{
    if (size > reply_capacity || !ChannelPayload__read_reply(self->payload, reply, size))
    {
        return Error(OptionalChannelReplySize, channel_invalid_reply, CHANNEL_INVALID_REPLY);
    }
    QueueMessage acknowledgement = ChannelControlMessage__new(CHANNEL_REQUEST_ACKNOWLEDGE, 0, 0, self->token);
    match_result(
        Queue__push(&self->control, &acknowledgement),
        success(code)
        {
            self->received_reply = None(ChannelReplySize);
            self->request_pending = false;
            return Success(OptionalChannelReplySize, Some(ChannelReplySize, size));
        },
        error(code, message)
        {
            if (code == QUEUE_FULL || code == QUEUE_PENDING)
            {
                return Success(OptionalChannelReplySize, None(ChannelReplySize));
            }
            return Error(OptionalChannelReplySize, channel_transport_failed, code);
        }
    );
    return Error(OptionalChannelReplySize, channel_transport_failed, CHANNEL_TRANSPORT_FAILED);
}

Result(OptionalChannelReplySize) Channel__try_poll_reply(ChannelPtr self, ChannelDataPtr reply, size_t reply_capacity)
{
    if (!self->request_pending)
    {
        return Error(OptionalChannelReplySize, channel_invalid_reply, CHANNEL_INVALID_REPLY);
    }
    match_optional(
        self->received_reply,
        some(size) { return Channel__acknowledge(self, reply, reply_capacity, size); },
        none_do_nothing()
    );
    match_result(
        Queue__pop(&self->control),
        success(response)
        {
            if (response.kind != CHANNEL_RESPONSE_REPLIED || response.arg2 != self->token ||
                response.arg1 >= CHANNEL_PAYLOAD_CAPACITY)
            {
                return Error(OptionalChannelReplySize, channel_invalid_reply, CHANNEL_INVALID_REPLY);
            }
            // Retain the consumed reply until the acknowledgement can be published.
            self->received_reply = Some(ChannelReplySize, response.arg1);
            return Channel__acknowledge(self, reply, reply_capacity, response.arg1);
        },
        error(code, message)
        {
            if (code == QUEUE_EMPTY || code == QUEUE_PENDING)
            {
                return Success(OptionalChannelReplySize, None(ChannelReplySize));
            }
            return Error(OptionalChannelReplySize, channel_transport_failed, code);
        }
    );
    return Error(OptionalChannelReplySize, channel_transport_failed, CHANNEL_TRANSPORT_FAILED);
}
