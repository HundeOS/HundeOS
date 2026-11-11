#include "channel_native.h"
#include "buffer/client.h"
#include "channel/server.h"

typedef struct RustChannelListener
{
    QueueArrivalsPtr arrivals;
} RustChannelListener;

typedef RustChannelListener * RustChannelListenerPtr;

RustNativeLayout hunde_channel_listener_layout(void)
{
    return RustNativeLayout__new(sizeof(RustChannelListener), _Alignof(RustChannelListener));
}

HundeStatusResult hunde_channel_listen(void * storage, HundeByteSpan name)
{
    RustChannelListenerPtr self = storage;
    BString service = BString__from_chars((const char *)name.bytes, name.length);
    match_result(
        Queue__listen(&service),
        success(arrivals)
        {
            self->arrivals = arrivals;
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

RustNativeValue hunde_channel_arrival(void * storage, uint64_t index)
{
    RustChannelListenerPtr self = storage;
    match_optional(
        QueueConnectionSlots__at(&self->arrivals->connections, index),
        some(connection)
        {
            return RustNativeValue__new(HundeStatusResult__success(), connection);
        },
        none_do_nothing()
    );
    return RustNativeValue__new(HundeStatusResult__new_pending(), 0);
}

RustNativeLayout hunde_channel_server_layout(void)
{
    return RustNativeLayout__new(sizeof(ChannelServer), _Alignof(ChannelServer));
}

HundeStatusResult hunde_channel_accept(void * storage, uint64_t connection)
{
    ChannelServerPtr self = storage;
    match_result(
        ChannelServer__accept(connection),
        success(channel)
        {
            *self = channel;
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            QueueConnection__close(connection);
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

static HundeStatusResult RustChannel__status(ChannelCode code)
{
    if (code == CHANNEL_PENDING)
    {
        return HundeStatusResult__new_pending();
    }
    return code == CHANNEL_OK ? HundeStatusResult__success() : RustNative__error(code);
}

RustNativeLayout hunde_channel_client_layout(void)
{
    return RustNativeLayout__new(sizeof(Channel), _Alignof(Channel));
}

HundeStatusResult hunde_channel_open(void * storage, HundeByteSpan endpoint)
{
    ChannelPtr self = storage;
    BString name = BString__from_chars((const char *)endpoint.bytes, endpoint.length);
    match_result(
        Channel__open(ServiceDirectory__shared(), &name),
        success(channel)
        {
            *self = channel;
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustChannel__status(code);
        }
    );
    return RustNative__unknown_error();
}

HundeStatusResult hunde_channel_send(void * storage, HundeByteSpan request)
{
    match_result(
        Channel__try_request(storage, (ConstChannelDataPtr)request.bytes, request.length),
        success(code)
        {
            return RustChannel__status(code);
        },
        error(code, message)
        {
            return RustChannel__status(code);
        }
    );
    return RustNative__unknown_error();
}

RustNativeValue hunde_channel_receive(void * storage, uint8_t * target, uint64_t capacity)
{
    match_result(
        Channel__try_poll_reply(storage, (ChannelDataPtr)target, capacity),
        success(optional_size)
        {
            match_optional(
                optional_size,
                some(size)
                {
                    return RustNativeValue__new(HundeStatusResult__success(), size);
                },
                none_do_nothing()
            );
            return RustNativeValue__new(RustChannel__status(CHANNEL_PENDING), 0);
        },
        error(code, message)
        {
            return RustNativeValue__new(RustNative__error(code), 0);
        }
    );
    return RustNativeValue__new(RustNative__unknown_error(), 0);
}

uint64_t hunde_channel_server_pid(void * storage)
{
    return Channel__server_pid(storage);
}

void hunde_channel_close(void * storage)
{
    Channel__close(storage);
}

RustNativeValue hunde_channel_server_read(void * storage, uint8_t * target, uint64_t capacity)
{
    match_result(
        ChannelServer__poll(storage),
        success(optional_request)
        {
            match_optional(
                optional_request,
                some(request)
                {
                    if (request.size > capacity ||
                        !ChannelServer__read_request(storage, (ChannelDataPtr)target, request.size))
                    {
                        return RustNativeValue__new(RustNative__error(CHANNEL_PAYLOAD_TOO_LARGE), 0);
                    }
                    return RustNativeValue__new(HundeStatusResult__success(), request.size);
                },
                none_do_nothing()
            );
            return RustNativeValue__new(RustChannel__status(CHANNEL_PENDING), 0);
        },
        error(code, message)
        {
            return RustNativeValue__new(RustNative__error(code), 0);
        }
    );
    return RustNativeValue__new(RustNative__unknown_error(), 0);
}

HundeStatusResult hunde_channel_server_reply(void * storage, HundeByteSpan reply)
{
    match_result(
        ChannelServer__reply(storage, (ConstChannelDataPtr)reply.bytes, reply.length),
        success(code)
        {
            return RustChannel__status(code);
        },
        error(code, message)
        {
            return RustChannel__status(code);
        }
    );
    return RustNative__unknown_error();
}

uint64_t hunde_channel_transport_pid(void * storage)
{
    return ChannelServer__transport_pid(storage);
}

uint32_t hunde_channel_server_alive(void * storage)
{
    return ChannelServer__status(storage) == QUEUE_OK;
}

void hunde_channel_server_close(void * storage)
{
    ChannelServer__close(storage);
}

HundeStatusResult hunde_buffer_validate(ConstBufferDescriptorPtr buffer, uint64_t peer, uint32_t direction)
{
    return BufferClient__validate(buffer, peer, (BufferDirection)direction) ? HundeStatusResult__success()
                                                                            : RustNative__error(BUFFER_INVALID);
}
