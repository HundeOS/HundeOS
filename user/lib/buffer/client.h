#ifndef USER_BUFFER_CLIENT_H
#define USER_BUFFER_CLIENT_H
#include "ipc/buffer.h"
#include "channel/channel.h"
#include "syscalls/mem.h"
int64_t BufferMemory__operation(MemOperation operation, uint64_t first, uint64_t second, uint64_t third,
                                uint64_t fourth);
Result(BufferDescriptor)
    BufferClient__acquire(ConstBStringPtr service, BufferParticipant client, BufferDirection direction);
BufferStatus BufferClient__release(ConstBufferDescriptorPtr buffer);
BufferStatus BufferClient__release_pair(BufferPairPtr buffers, BufferParticipant client);
bool BufferClient__validate(ConstBufferDescriptorPtr buffer, BufferParticipant peer, BufferDirection direction);
#endif
