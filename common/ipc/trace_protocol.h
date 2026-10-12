#ifndef HUND_TRACE_PROTOCOL_H
#define HUND_TRACE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "buffer.h"
#include "channel_protocol.h"
#include "io_trace.h"

#define HUND_VERFOLGEN_SERVICE "hund-verfolgen"

typedef enum TraceOperation
{
    TRACE_OPERATION_REGISTER_IO = 3,
    TRACE_OPERATION_DRAIN_IO = 4,
} TraceOperation;

typedef enum TraceStatus
{
    TRACE_OK = 0,
    TRACE_INVALID_REQUEST = -1,
    TRACE_UNAVAILABLE = -2,
    TRACE_BUFFER_FULL = -3,
    TRACE_IO_FAILED = -4,
} TraceStatus;

typedef struct TraceRequest
{
    TraceOperation operation;
} TraceRequest;

typedef TraceRequest * TraceRequestPtr;
typedef const TraceRequest * ConstTraceRequestPtr;

typedef struct TraceReply
{
    TraceStatus status;
    BufferDescriptor buffer;
} TraceReply;

typedef const TraceReply * ConstTraceReplyPtr;

static inline TraceRequest TraceRequest__new(TraceOperation operation)
{
    return (TraceRequest){.operation = operation};
}

static inline TraceRequest TraceRequest__register_io(void)
{
    return TraceRequest__new(TRACE_OPERATION_REGISTER_IO);
}

static inline TraceRequest TraceRequest__drain_io(void)
{
    return TraceRequest__new(TRACE_OPERATION_DRAIN_IO);
}

static inline TraceReply TraceReply__new(TraceStatus status)
{
    return (TraceReply){.status = status};
}

_Static_assert(sizeof(TraceRequest) < CHANNEL_PAYLOAD_CAPACITY, "Trace request exceeds channel payload");
_Static_assert(sizeof(TraceReply) < CHANNEL_PAYLOAD_CAPACITY, "Trace reply exceeds channel payload");
_Static_assert(sizeof(IOTraceRing) <= BUFFER_SIZE, "I/O trace ring exceeds shared buffer");

#endif
