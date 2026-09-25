#include "trace_buffer.h"

#include <string.h>

TraceBuffer TraceBuffer__new(void)
{
    return (TraceBuffer){0};
}

bool TraceBuffer__append(TraceBufferPtr self, ConstBStringPtr line)
{
    size_t length = (size_t)BString__length(line);
    bool needs_newline = length == 0 || BString__char_at_or(line, (StringPos)length - 1, '\0') != '\n';
    size_t required = length + (needs_newline ? 1U : 0U);
    if (required > TRACE_BUFFER_CAPACITY - self->length)
    {
        self->dropped++;
        return false;
    }

    memcpy(self->bytes + self->length, BString__cstr(line), length);
    self->length += length;
    if (needs_newline)
    {
        self->bytes[self->length++] = '\n';
    }
    return true;
}

bool TraceBuffer__is_empty(TraceBufferPtr self)
{
    return self->length == 0;
}

size_t TraceBuffer__length(TraceBufferPtr self)
{
    return self->length;
}

const uint8_t * TraceBuffer__data(TraceBufferPtr self)
{
    return self->bytes;
}

uint64_t TraceBuffer__dropped(TraceBufferPtr self)
{
    return self->dropped;
}

bool TraceBuffer__consume(TraceBufferPtr self, size_t length)
{
    if (length > self->length)
    {
        return false;
    }

    memmove(self->bytes, self->bytes + length, self->length - length);
    self->length -= length;
    return true;
}

uint64_t TraceBuffer__clear(TraceBufferPtr self)
{
    uint64_t dropped = self->dropped;
    self->length = 0;
    self->dropped = 0;
    return dropped;
}
