#include "stream.h"

#include "debug.h"
#include "subsystems/io/io_subsystem.h"
#include "../../../kalloc.h"
#include "../../../types/kstring.h"
#include "../../../types/result.h"

FixedArrayBodyDefine(StreamParkedContexts);

static StreamState stream_state = (StreamState) {
    .state = NEW_IO_STATE,
    .write_buffer = EmptyRingBuffer(IOBuffer),
    .read_buffer = EmptyRingBuffer(IOBuffer),
    .data_available = false,
    .eof = false,
    .parked_contexts = EmptyArray(StreamParkedContexts),
    .lock = MUTEX_INIT,
    .handle_counter = 0,
};

static bool Stream__handle_match(IOContextPtr * context_p, void * handle_p)
{
    IOContextPtr context = *context_p;
    IOInternalHandle handle = *(IOInternalHandle *) handle_p;

    return handle == device_internal_handle(Unwrap(IOHandle, context->handle, -1));
}

static Result(IOInternalHandle) Stream__open(IOStatePtr state, KString path, Flags flags)
{
    StreamStatePtr self = (StreamStatePtr) state;
    IOInternalHandle handle = -1;
    CriticalSection(self->lock,
    {
        handle = ++self->handle_counter;
    });
    return Success(IOInternalHandle, handle);
}

static Result(IOInternalHandle) Stream__close(IOStatePtr state, IOInternalHandle handle)
{
    StreamStatePtr self = (StreamStatePtr) state;

    CriticalSection(self->lock,
    {
        IOBuffer__reset(&self->read_buffer);
        IOBuffer__reset(&self->write_buffer);
        self->data_available = false;
        self->eof = false;

        FindResultMatch(StreamParkedContexts, StreamParkedContexts__find(&self->parked_contexts, Stream__handle_match, &handle))
            IsSome(found,
            {
                ResultMatch(CollectionCode, StreamParkedContexts__remove(&self->parked_contexts, *found.value))
                    IsError(code, message, __d_println(IODEV, "no parked context found for handle"));
                End
            })
        End
    });

    return Success(IOInternalHandle, handle);
}

static Result(IOBufferPtr) Stream__read(
    IOStatePtr state, IOInternalHandle handle, size_t size, ExternalBuffer target
)
{
    StreamStatePtr self = (StreamStatePtr) state;
    if (IOBuffer__item_count(&self->read_buffer) == 0 && self->eof)
    {
        return Error(IOBufferPtr, kstring("EOF"), IO_EOF);
    }

    if (!self->data_available || IOBuffer__item_count(&self->read_buffer) == 0)
    {
        return Error(IOBufferPtr, kstring("wait for IO"), IO_WAIT_FOR_DATA);
    }
    return Success(IOBufferPtr, &self->read_buffer);
}

static Result(ByteCount) Stream__write(IOStatePtr state, IOInternalHandle handle, DataPtr data, size_t size)
{
    StreamStatePtr self = (StreamStatePtr) state;
    DataPtr cursor = data;

    CriticalSection(self->lock,
    {
        self->eof = false;
        for (size_t i = 0; i < size; i++)
        {
            ResultMatch(Pos, IOBuffer__push(&self->write_buffer, cursor[i]))
                IsError(code, message, _cs_return Error(ByteCount, message, code))
            End
        }
    });

    return Success(ByteCount, size);
}

static Result(ParkStatus) Stream__park(IOStatePtr state, IOContextPtr parked_context)
{
    StreamStatePtr self = (StreamStatePtr) state;

    CriticalSection(self->lock,
    {
        ResultMatch(CollectionCode, StreamParkedContexts__push(&self->parked_contexts, parked_context))
            IsError(code, message, _cs_return Error(ParkStatus, message, code))
        End
        __d_println(IODEV, "parked stream context\n");
    });

    return Success(ParkStatus, IO_PARK_OK);
}

static Result(ParkStatus) Stream__unpark(IOStatePtr state)
{
    StreamStatePtr self = (StreamStatePtr) state;

    CriticalSection(self->lock,
    {
        Match(IOContextPtr, StreamParkedContexts__pop(&self->parked_contexts))
            IsNone(_cs_return Error(ParkStatus, kstring("No parked stream context"), ARRAY_EMPTY))
        End
    });

    return Success(ParkStatus, IO_UNPARK_OK);
}

static Result(IOServerBuffer) Stream__server_buffer(IOStatePtr state)
{
    StreamStatePtr self = (StreamStatePtr) state;
    IOServerBuffer buffer = (IOServerBuffer) {
        .data = NULL,
        .capacity = 0,
    };

    CriticalSection(self->lock,
    {
        size_t write_pos = self->read_buffer.write_pos % self->read_buffer.buffer_size;
        size_t free_space = IOBuffer__free_space(&self->read_buffer);
        size_t contiguous_space = self->read_buffer.buffer_size - write_pos;

        buffer.data = (IOServerBufferDataPtr) &self->read_buffer.buffer[write_pos];
        buffer.capacity = free_space < contiguous_space ? free_space : contiguous_space;
    });

    return Success(IOServerBuffer, buffer);
}

static Result(IOServerBuffer) Stream__server_write_buffer(IOStatePtr state)
{
    StreamStatePtr self = (StreamStatePtr) state;
    IOServerBuffer buffer;
    buffer.data = NULL;
    buffer.capacity = 0;

    CriticalSection(self->lock,
    {
        size_t read_pos = self->write_buffer.read_pos % self->write_buffer.buffer_size;
        size_t item_count = IOBuffer__item_count(&self->write_buffer);
        size_t contiguous = self->write_buffer.buffer_size - read_pos;
        buffer.data = (IOServerBufferDataPtr) &self->write_buffer.buffer[read_pos];
        buffer.capacity = item_count < contiguous ? item_count : contiguous;
    });

    return Success(IOServerBuffer, buffer);
}

static Optional(IOContextPtr) Stream__data_available(IOStatePtr state, size_t byte_count)
{
    StreamStatePtr self = (StreamStatePtr) state;
    Optional(IOContextPtr) parked_context = None(IOContextPtr);

    CriticalSection(self->lock,
    {
        size_t free_space = IOBuffer__free_space(&self->read_buffer);
        if (byte_count > free_space)
        {
            byte_count = free_space;
        }

        self->read_buffer.write_pos += byte_count;
        self->data_available = true;
        self->eof = false;
        parked_context = StreamParkedContexts__at(&self->parked_contexts, -1);
    });

    return parked_context;
}

static Result(IOStatus) Stream__data_consumed(IOStatePtr state, size_t byte_count)
{
    StreamStatePtr self = (StreamStatePtr) state;
    CriticalSection(self->lock,
    {
        size_t item_count = IOBuffer__item_count(&self->write_buffer);
        if (byte_count > item_count)
        {
            byte_count = item_count;
        }
        self->write_buffer.read_pos += byte_count;
    });
    return Success(IOStatus, IO_OK);
}

static Optional(IOContextPtr) Stream__eof(IOStatePtr state)
{
    StreamStatePtr self = (StreamStatePtr) state;
    Optional(IOContextPtr) parked_context = None(IOContextPtr);

    CriticalSection(self->lock,
    {
        self->eof = true;
        self->data_available = true;
        parked_context = StreamParkedContexts__at(&self->parked_contexts, -1);
    });

    return parked_context;
}

IOPtr Stream__heap_new()
{
    IOPtr result = v_ptr(Kernel__alloc(sizeof(IO)), IO);

    *result = (IO) {
        .read = Stream__read,
        .write = Stream__write,
        .open = Stream__open,
        .close = Stream__close,
        .park = Stream__park,
        .unpark = Stream__unpark,
        .server_buffer = Stream__server_buffer,
        .server_write_buffer = Stream__server_write_buffer,
        .data_available = Stream__data_available,
        .data_consumed = Stream__data_consumed,
        .eof = Stream__eof,
    };

    return result;
}

StreamStatePtr StreamState__instance()
{
    return &stream_state;
}

StreamStatePtr StreamState__heap_new()
{
    StreamStatePtr result = ptr(Kernel__alloc_kernel(sizeof(StreamState)), StreamState);
    *result = (StreamState) {
        .state = NEW_IO_STATE,
        .write_buffer = EmptyRingBuffer(IOBuffer),
        .read_buffer = EmptyRingBuffer(IOBuffer),
        .data_available = false,
        .eof = false,
        .parked_contexts = EmptyArray(StreamParkedContexts),
        .lock = MUTEX_INIT,
        .handle_counter = 0,
    };
    return result;
}
