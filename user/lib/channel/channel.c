#include "channel.h"

static BString channel_service_unavailable = bstr("Channel service unavailable");
static BString channel_transport_failed = bstr("Channel transport failed");
static BString channel_invalid_reply = bstr("Invalid channel reply");

static Result(QueueMessage) Channel__receive(QueuePtr queue)
{
    Queue__wait_readable(queue);
    match_result(
        Queue__pop(queue),
        success(message)
        {
            return Success(QueueMessage, message);
        },
        error(code, message)
        {
            return Error(QueueMessage, channel_transport_failed, code);
        }
    );
}

static Result(QueueCode) Channel__send(QueuePtr queue, ConstQueueMessagePtr message)
{
    Queue__wait_writable(queue);
    match_result(
        Queue__push(queue, message),
        success(code)
        {
            return Success(QueueCode, code);
        },
        error(code, error_message)
        {
            return Error(QueueCode, channel_transport_failed, code);
        }
    );
}

static Result(Channel) Channel__connect(ServiceDirectoryPtr directory, QueueEndpointID target)
{
    match_optional(
        ServiceDirectory__find_endpoint(directory, &bstr(CHANNEL_WARDEN_SERVICE_NAME)),
        some(warden)
        {
            match_result(
                Queue__connect(warden),
                success(control)
                {
                    QueueMessage open =
                        ChannelControlMessage__new(CHANNEL_REQUEST_OPEN, target, 0, Queue__connection(&control));
                    match_result(
                        Channel__send(&control, &open),
                        success_do_nothing(),
                        error(code, message)
                        {
                            QueueConnection__close(Queue__connection(&control));
                            return Error(Channel, message, code);
                        }
                    );
                    match_result(
                        Channel__receive(&control),
                        success(reply)
                        {
                            if (reply.kind != CHANNEL_RESPONSE_OPEN || reply.arg2 != Queue__connection(&control) ||
                                reply.arg0 == 0)
                            {
                                QueueConnection__close(Queue__connection(&control));
                                return Error(Channel, channel_invalid_reply, CHANNEL_INVALID_REPLY);
                            }
                            return Success(Channel, Channel__new(&control, (ChannelPayloadPtr)reply.arg0));
                        },
                        error(code, message)
                        {
                            QueueConnection__close(Queue__connection(&control));
                            return Error(Channel, message, code);
                        }
                    );
                },
                error(code, message)
                {
                    return Error(Channel, channel_transport_failed, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(Channel, channel_service_unavailable, CHANNEL_UNAVAILABLE);
}

Result(Channel) Channel__open(ServiceDirectoryPtr directory, ConstBStringPtr target_service)
{
    match_optional(
        ServiceDirectory__find_endpoint(directory, target_service),
        some(target)
        {
            uint64_t server = unwrap_optional(ServiceDirectory__find_service_pid(directory, target_service), 0);
            if (server == 0)
            {
                return Error(Channel, channel_service_unavailable, CHANNEL_UNAVAILABLE);
            }
            match_result(
                Channel__connect(directory, target),
                success(channel)
                {
                    if (unwrap_optional(ServiceDirectory__find_endpoint(directory, target_service), 0) != target)
                    {
                        Channel__close(&channel);
                        return Error(Channel, channel_service_unavailable, CHANNEL_UNAVAILABLE);
                    }
                    channel.server_pid = server;
                    return Success(Channel, channel);
                },
                error(code, message)
                {
                    return Error(Channel, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(Channel, channel_service_unavailable, CHANNEL_UNAVAILABLE);
}

Result(ChannelCode) Channel__request(ChannelPtr self, ConstChannelDataPtr request, size_t request_size)
{
    Optional(RequestTicket) parent = ChannelRequestContext__current();
    while (true)
    {
        match_result(
            Channel__try_request_with_parent(self, request, request_size, &parent),
            success(code)
            {
                if (code != CHANNEL_PENDING)
                {
                    return Success(ChannelCode, code);
                }
                Queue__wait_writable(&self->control);
            },
            error(code, message)
            {
                return Error(ChannelCode, message, code);
            }
        );
    }
}

Result(OptionalChannelReplySize) Channel__poll_reply(ChannelPtr self, ChannelDataPtr reply, size_t reply_capacity)
{
    while (true)
    {
        match_result(
            Channel__try_poll_reply(self, reply, reply_capacity),
            success(size)
            {
                match_optional(
                    self->received_reply,
                    some(pending_size)
                    {
                        Queue__wait_writable(&self->control);
                    },
                    none()
                    {
                        return Success(OptionalChannelReplySize, size);
                    }
                );
            },
            error(code, message)
            {
                return Error(OptionalChannelReplySize, message, code);
            }
        );
    }
}

Result(ChannelReplySize) Channel__submit(ChannelPtr self, ConstChannelDataPtr request, size_t request_size,
                                         ChannelDataPtr reply, size_t reply_capacity)
{
    match_result(
        Channel__request(self, request, request_size),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ChannelReplySize, message, code);
        }
    );
    while (true)
    {
        Queue__wait_readable(&self->control);
        match_result(
            Channel__poll_reply(self, reply, reply_capacity),
            success(optional_size)
            {
                match_optional(
                    optional_size,
                    some(size)
                    {
                        return Success(ChannelReplySize, size);
                    },
                    none_do_nothing()
                );
            },
            error(code, message)
            {
                return Error(ChannelReplySize, message, code);
            }
        );
    }
}

void Channel__prepare_wait(void)
{
    Queue__prepare_wait();
}

void Channel__wait_event(void)
{
    Queue__wait_event();
}

ChannelCode Channel__close(ChannelPtr self)
{
    QueueCode code = QueueConnection__close(Queue__connection(&self->control));
    self->token = 0;
    self->received_reply = None(ChannelReplySize);
    self->request_pending = false;
    return code == QUEUE_OK ? CHANNEL_OK : CHANNEL_TRANSPORT_FAILED;
}
