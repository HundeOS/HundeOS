#include "buffer/client.h"
#include <stdlib.h>

// Host-backed leases substitute only the kernel mappings and manager transport.
static BufferID next_buffer = BUFFER_LIMIT;

Result(BufferDescriptor)
    BufferClient__acquire(ConstBStringPtr service, BufferParticipant client, BufferDirection direction)
{
    ChannelDataPtr data = calloc(1, BUFFER_SIZE);
    if (!data)
    {
        return Error(BufferDescriptor, bstr("Fake allocation failed"), BUFFER_NO_MEMORY);
    }
    return Success(
        BufferDescriptor,
        ((BufferDescriptor){
            .id = next_buffer++, .address = (Address)data, .capacity = BUFFER_SIZE, .direction = direction}));
}

bool BufferClient__validate(ConstBufferDescriptorPtr buffer, BufferParticipant peer, BufferDirection direction)
{
    return (peer == 8 || peer == 42) && buffer->id >= BUFFER_LIMIT && buffer->address != 0 &&
           buffer->capacity == BUFFER_SIZE && buffer->direction == direction;
}

BufferStatus BufferClient__release(ConstBufferDescriptorPtr buffer)
{
    return BUFFER_OK;
}

BufferStatus BufferClient__release_pair(BufferPairPtr buffers, BufferParticipant client)
{
    if (buffers->read.id != 0)
    {
        free((void *)buffers->read.address);
    }
    if (buffers->write.id != 0)
    {
        free((void *)buffers->write.address);
    }
    *buffers = (BufferPair){0};
    return BUFFER_OK;
}
