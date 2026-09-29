#ifndef HUND_TRACE_PROTOCOL_H
#define HUND_TRACE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "buffer.h"
#include "channel_protocol.h"
#include "io_trace.h"
#include "types/bstring.h"

#define HUND_VERFOLGEN_SERVICE "hund-verfolgen"
#define TRACE_TAG_CAPACITY 32U
#define TRACE_MESSAGE_CAPACITY 896U

typedef enum TraceOperation
{
    TRACE_OPERATION_RECORD = 1,
    TRACE_OPERATION_STORAGE_READY,
    TRACE_OPERATION_REGISTER_IO,
    TRACE_OPERATION_DRAIN_IO,
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
    uint16_t tag_length;
    uint16_t message_length;
    uint8_t tag[TRACE_TAG_CAPACITY];
    uint8_t message[TRACE_MESSAGE_CAPACITY];
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

static inline TraceRequest TraceRequest__record(ConstBStringPtr tag, ConstBStringPtr message)
{
    TraceRequest request = TraceRequest__new(TRACE_OPERATION_RECORD);
    request.tag_length = (uint16_t)BString__copy_to(tag, (char *)request.tag, (StringPos)sizeof(request.tag));
    request.message_length =
        (uint16_t)BString__copy_to(message, (char *)request.message, (StringPos)sizeof(request.message));
    return request;
}

static inline TraceRequest TraceRequest__storage_ready(void)
{
    return TraceRequest__new(TRACE_OPERATION_STORAGE_READY);
}

static inline TraceRequest TraceRequest__register_io(void)
{
    return TraceRequest__new(TRACE_OPERATION_REGISTER_IO);
}

static inline TraceRequest TraceRequest__drain_io(void)
{
    return TraceRequest__new(TRACE_OPERATION_DRAIN_IO);
}

static inline bool TraceRequest__valid_record(ConstTraceRequestPtr self)
{
    return self->operation == TRACE_OPERATION_RECORD && self->tag_length <= TRACE_TAG_CAPACITY &&
           self->message_length <= TRACE_MESSAGE_CAPACITY;
}

static inline BString TraceRequest__tag(ConstTraceRequestPtr self)
{
    return BString__from_chars((const char *)self->tag, self->tag_length);
}

static inline BString TraceRequest__message(ConstTraceRequestPtr self)
{
    return BString__from_chars((const char *)self->message, self->message_length);
}

static inline TraceReply TraceReply__new(TraceStatus status)
{
    return (TraceReply){.status = status};
}

_Static_assert(sizeof(TraceRequest) < CHANNEL_PAYLOAD_CAPACITY, "Trace request exceeds channel payload");
_Static_assert(sizeof(TraceReply) < CHANNEL_PAYLOAD_CAPACITY, "Trace reply exceeds channel payload");
_Static_assert(sizeof(IOTraceRing) <= BUFFER_SIZE, "I/O trace ring exceeds shared buffer");

#endif
