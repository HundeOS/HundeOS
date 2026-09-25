#ifndef HUND_VERFOLGEN_TRACE_BUFFER_H
#define HUND_VERFOLGEN_TRACE_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#include "types/bstring.h"

#define TRACE_BUFFER_CAPACITY (64U * 1024U)

typedef struct TraceBuffer
{
    uint8_t bytes[TRACE_BUFFER_CAPACITY];
    size_t length;
    uint64_t dropped;
} TraceBuffer;

typedef TraceBuffer * TraceBufferPtr;

TraceBuffer TraceBuffer__new(void);
bool TraceBuffer__append(TraceBufferPtr self, ConstBStringPtr line);
bool TraceBuffer__is_empty(TraceBufferPtr self);
size_t TraceBuffer__length(TraceBufferPtr self);
const uint8_t * TraceBuffer__data(TraceBufferPtr self);
uint64_t TraceBuffer__dropped(TraceBufferPtr self);
bool TraceBuffer__consume(TraceBufferPtr self, size_t length);
uint64_t TraceBuffer__clear(TraceBufferPtr self);

#endif
