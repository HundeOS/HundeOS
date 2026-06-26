#ifndef __KERNEL_SUBSYSTEMS_IO_STREAM_STREAM_H
#define __KERNEL_SUBSYSTEMS_IO_STREAM_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "../io.h"

FixedArrayDefine(IOContextPtr, 1024, StreamParkedContexts, Ptr__equals);

typedef struct StreamState
{
    IOState state;
    IOBuffer read_buffer;
    IOBuffer write_buffer;
    IOInternalHandle handle_counter;
    bool data_available;
    bool eof;

    StreamParkedContexts parked_contexts;

    mutex_t lock;
}
StreamState;

typedef StreamState * StreamStatePtr;

StreamStatePtr StreamState__instance();
StreamStatePtr StreamState__heap_new();
IOPtr Stream__heap_new();

#endif
