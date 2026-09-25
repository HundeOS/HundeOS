#ifndef HUND_BUFFER_H
#define HUND_BUFFER_H
#include "../types/address.h"
#include "../types/result.h"
#include "channel_protocol.h"

#define BUFFER_SERVICE_NAME "hunde-puffer"
#define BUFFER_SIZE (2ULL * 1024 * 1024)
#define BUFFER_LIMIT 256U
#define BUFFER_BASE 0x80000000ULL
#define BUFFER_EVENTS_ADDRESS 0x7f000000ULL
#define BUFFER_POOL_LIMIT 4U
typedef uint64_t BufferID;
typedef uint64_t BufferParticipant;

typedef enum BufferDirection
{
    BUFFER_READ = 1,
    BUFFER_WRITE = 2
} BufferDirection;

typedef enum BufferStatus
{
    BUFFER_OK = 0,
    BUFFER_INVALID = -1,
    BUFFER_DENIED = -2,
    BUFFER_NO_MEMORY = -3,
    BUFFER_BUSY = -4,
    BUFFER_UNAVAILABLE = -5
} BufferStatus;

typedef enum BufferState
{
    BUFFER_CLIENT_ATTACHED = 1,
    BUFFER_SERVER_ATTACHED = 2,
    BUFFER_RETIRING = 4,
    BUFFER_RETAINED = 8
} BufferState;

typedef struct BufferDescriptor
{
    BufferID id;
    Address address;
    uint64_t capacity;
    BufferDirection direction;
} BufferDescriptor;

typedef BufferDescriptor * BufferDescriptorPtr;
typedef const BufferDescriptor * ConstBufferDescriptorPtr;
ResultDef(BufferDescriptor);
ResultDef(BufferStatus);

typedef struct BufferPair
{
    BufferDescriptor read;
    BufferDescriptor write;
} BufferPair;

typedef BufferPair * BufferPairPtr;

static inline BufferPair BufferPair__new(void)
{
    return (BufferPair){0};
}

static inline BufferDescriptor BufferDescriptor__new(BufferID id, BufferDirection direction)
{
    return (BufferDescriptor){id, BUFFER_BASE + (id % BUFFER_LIMIT) * BUFFER_SIZE, BUFFER_SIZE, direction};
}

static inline bool BufferDescriptor__valid(ConstBufferDescriptorPtr self, BufferDirection direction)
{
    return self->id >= BUFFER_LIMIT && self->address == BUFFER_BASE + (self->id % BUFFER_LIMIT) * BUFFER_SIZE &&
           self->capacity == BUFFER_SIZE && self->direction == direction;
}

static inline bool BufferDescriptor__contains(ConstBufferDescriptorPtr self, uint64_t offset, uint64_t size)
{
    return offset <= self->capacity && size <= self->capacity - offset;
}

typedef enum BufferOperation
{
    BUFFER_ACQUIRE = 1,
    BUFFER_RELEASE,
    BUFFER_RELEASE_PAIR
} BufferOperation;

typedef struct BufferRequest
{
    uint64_t service_length;
    uint8_t service_data[256];
    BufferOperation operation;
    BufferDirection direction;
    BufferParticipant client;
    BufferID id;
    BufferID second;
} BufferRequest;

typedef BufferRequest * BufferRequestPtr;
typedef const BufferRequest * ConstBufferRequestPtr;
typedef BufferID * BufferIDPtr;

typedef struct BufferReply
{
    BufferStatus status;
    BufferDescriptor buffer;
} BufferReply;

static inline BufferRequest BufferRequest__new(BufferOperation operation, BufferParticipant client,
                                               BufferDirection direction, BufferID id, BufferID second)
{
    return (BufferRequest){
        .operation = operation, .client = client, .direction = direction, .id = id, .second = second};
}

static inline BufferReply BufferReply__new(BufferStatus status)
{
    return (BufferReply){.status = status};
}

static inline bool BufferRequest__set_service(BufferRequestPtr self, ConstBStringPtr service)
{
    if (BString__length(service) >= sizeof(self->service_data))
    {
        return false;
    }
    self->service_length = BString__copy_to(service, (char *)self->service_data, sizeof(self->service_data));
    return true;
}

static inline BString BufferRequest__service(ConstBufferRequestPtr self)
{
    return BString__from_chars((const char *)self->service_data,
                               self->service_length < sizeof(self->service_data) ? self->service_length : 0);
}

_Static_assert(sizeof(BufferRequest) < CHANNEL_PAYLOAD_CAPACITY, "Buffer request exceeds channel payload");
_Static_assert(sizeof(BufferReply) < CHANNEL_PAYLOAD_CAPACITY, "Buffer reply exceeds channel payload");
#endif
