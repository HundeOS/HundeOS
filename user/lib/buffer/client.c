#include "client.h"
#include "sys/queue_wait.h"

static Channel manager;
static bool connected;
static QueueWaitGate manager_gate = QUEUE_WAIT_GATE_INITIALIZER;

static BufferReply BufferClient__transact(BufferRequest request)
{
    BufferReply failed = BufferReply__new(BUFFER_UNAVAILABLE);
    if (!connected)
    {
        match_result(
            Channel__open(ServiceDirectory__shared(), &bstr(BUFFER_SERVICE_NAME)),
            success(channel)
            {
                manager = channel;
                connected = true;
            },
            error(code, message)
            {
                return failed;
            }
        );
    }
    BufferReply reply;
    match_result(
        Channel__submit(&manager, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&reply,
                        sizeof(reply)),
        success(size)
        {
            if (size == sizeof(reply))
            {
                return reply;
            }
        },
        error(code, message)
        {
            Channel__close(&manager);
            connected = false;
            return failed;
        }
    );
    Channel__close(&manager);
    connected = false;
    return failed;
}

static BufferReply BufferClient__submit(BufferRequest request)
{
    match_result(
        QueueWaitGate__acquire(&manager_gate),
        success(value)
        {
            BufferReply reply = BufferClient__transact(request);
            QueueWaitGate__release(&manager_gate);
            return reply;
        },
        error(code, message)
        {
            return BufferReply__new(BUFFER_UNAVAILABLE);
        }
    );
    return BufferReply__new(BUFFER_UNAVAILABLE);
}

Result(BufferDescriptor)
    BufferClient__acquire(ConstBStringPtr service, BufferParticipant client, BufferDirection direction)
{
    BufferRequest request = BufferRequest__new(BUFFER_ACQUIRE, client, direction, 0, 0);
    if (!BufferRequest__set_service(&request, service))
    {
        return Error(BufferDescriptor, bstr("Service name too long"), BUFFER_INVALID);
    }
    BufferReply reply = BufferClient__submit(request);
    if (reply.status != BUFFER_OK || !BufferClient__validate(&reply.buffer, client, direction))
    {
        if (reply.status == BUFFER_OK)
        {
            BufferClient__release(&reply.buffer);
        }
        return Error(BufferDescriptor, bstr("Buffer allocation failed"),
                     reply.status == BUFFER_OK ? BUFFER_INVALID : reply.status);
    }
    return Success(BufferDescriptor, reply.buffer);
}

BufferStatus BufferClient__release(ConstBufferDescriptorPtr buffer)
{
    if (buffer->id == 0)
    {
        return BUFFER_OK;
    }
    return BufferClient__submit(BufferRequest__new(BUFFER_RELEASE, 0, 0, buffer->id, 0)).status;
}

BufferStatus BufferClient__release_pair(BufferPairPtr buffers, BufferParticipant client)
{
    if (buffers->read.id == 0 && buffers->write.id == 0)
    {
        return BUFFER_OK;
    }
    BufferReply reply =
        BufferClient__submit(BufferRequest__new(BUFFER_RELEASE_PAIR, client, 0, buffers->read.id, buffers->write.id));
    if (reply.status == BUFFER_OK)
    {
        *buffers = BufferPair__new();
    }
    return reply.status;
}

bool BufferClient__validate(ConstBufferDescriptorPtr buffer, BufferParticipant peer, BufferDirection direction)
{
    return BufferDescriptor__valid(buffer, direction) &&
           BufferMemory__operation(MEM_BUFFER_VALIDATE, buffer->id, direction, peer, 0) == (int64_t)buffer->address;
}
