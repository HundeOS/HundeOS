#ifndef HUND_ROHR_PIPE_H
#define HUND_ROHR_PIPE_H

#include "io/protocol.h"
#include "types/byte_ring.h"
#include "types/fixed_array.h"

#define PIPE_CAPACITY (64U * 1024U)
#define PIPE_ATOMIC_WRITE 4096U

typedef uint64_t PipeID;
typedef PipeID * PipeIDPtr;

typedef enum PipeEndpointKind
{
    PIPE_ENDPOINT_READ,
    PIPE_ENDPOINT_WRITE,
} PipeEndpointKind;

typedef struct PipeObject
{
    PipeID id;
    uint8_t * storage;
    ByteRing bytes;
    uint64_t readers;
    uint64_t writers;
} PipeObject;

typedef PipeObject * PipeObjectPtr;
OptionalDef(PipeObject);
OptionalDef(PipeObjectPtr);
bool PipeObject__equals(void * first, void * second);
FixedArrayDefine(PipeObject, 64, PipeObjects, PipeObject__equals);

typedef struct PipeEndpoint
{
    IOHandle handle;
    PipeID pipe;
    QueueConnectionID session;
    uint64_t pid;
    PipeEndpointKind kind;
} PipeEndpoint;

typedef PipeEndpoint * PipeEndpointPtr;
OptionalDef(PipeEndpoint);
OptionalDef(PipeEndpointPtr);
bool PipeEndpoint__equals(void * first, void * second);
FixedArrayDefine(PipeEndpoint, 128, PipeEndpoints, PipeEndpoint__equals);

typedef struct PipeService
{
    PipeObjects pipes;
    PipeEndpoints endpoints;
    PipeID next_pipe;
    IOHandle next_handle;
} PipeService;

typedef PipeService * PipeServicePtr;

PipeService PipeService__new(void);
IOReply PipeService__execute(PipeServicePtr self, IORequestPtr request);
void PipeService__release(PipeServicePtr self, QueueConnectionID session);

#endif
