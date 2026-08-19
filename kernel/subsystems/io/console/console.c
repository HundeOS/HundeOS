#include "console.h"
#include "debug.h"
#include "arch/basic_io.h"
#include "../../../types/result.h"
#include "../../../types/bstring.h"
#include "../../../kalloc.h"
#include "subsystems/io/io_subsystem.h"

FixedArrayBodyDefine(ParkedContexts);

static Result(IOInternalHandle) Console__open(IOStatePtr state, BString path, Flags flags)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    IOInternalHandle handle = -1;
    CriticalSection(self->lock,
    {
        handle = ++self->handle_counter;
    });
    return Success(IOInternalHandle, handle);
}

bool handle_match(IOContextPtr * context_p, void * handle_p)
{
    IOContextPtr context = *context_p;
    IOInternalHandle handle = *(IOInternalHandle *) handle_p;

    return handle == device_internal_handle(Unwrap(IOHandle, context->handle, -1));
}

static Result(IOInternalHandle) Console__close(IOStatePtr state, IOInternalHandle handle)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;

    CriticalSection(self->lock,
    {
        IOBuffer__reset(&self->read_buffer);
        IOBuffer__reset(&self->write_buffer);

        FindResultMatch(ParkedContexts, ParkedContexts__find(&self->parked_contexts, handle_match, &handle))
            IsSome(found,
            {
                ResultMatch(CollectionCode, ParkedContexts__remove(&self->parked_contexts, *found.value))
                    IsError(code, message, __d_println(IODEV, "no parked context found for handle"));
                End
            })
        End
    });

    return Success(IOInternalHandle, handle);
}

static Result(IOBufferPtr) Console__read(
    IOStatePtr state, IOInternalHandle handle, size_t size, ExternalBuffer target
)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    if (IOBuffer__item_count(&self->read_buffer) == 0)
    {
        return Error(IOBufferPtr, bstring("wait for IO"), IO_WAIT_FOR_DATA);
    }
    return Success(IOBufferPtr, &self->read_buffer);
}

static Result(ByteCount) Console__write(IOStatePtr state, IOInternalHandle handle, DataPtr data, size_t size)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    DataPtr cursor = data;

    CriticalSection(self->lock,
    {
        for (size_t i = 0; i < size; i++)
        {
            char out[2];
            out[0] = cursor[i];
            out[1] = '\0';
            ResultMatch(Pos, IOBuffer__push(&self->write_buffer, cursor[i]))
                IsError(code, message, _cs_return Error(ByteCount, message, code))
            End
            BasicIO__puts(out);
        }
    });

    return Success(ByteCount, size);
}

static Result(IOServerBuffer) Console__server_write_buffer(IOStatePtr state)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    IOServerBuffer buffer = (IOServerBuffer) {
        .data = NULL,
        .capacity = 0,
    };

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

static Result(IOStatus) Console__data_consumed(IOStatePtr state, size_t byte_count)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
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

static Result(ParkStatus) Console__park(IOStatePtr state, IOContextPtr parked_context)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;

    CriticalSection(self->lock,
    {
        ResultMatch(CollectionCode, ParkedContexts__push(&self->parked_contexts, parked_context))
            IsError(code, message, _cs_return Error(ParkStatus, message, code))
        End
        __d_println(IODEV, "parked console context\n");
    });

    return Success(ParkStatus, IO_PARK_OK);
}

static Result(ParkStatus) Console__unpark(IOStatePtr state)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;

    CriticalSection(self->lock,
    {
        Match(IOContextPtr, ParkedContexts__pop(&self->parked_contexts))
            IsNone(_cs_return Error(ParkStatus, bstring("No parked console context"), ARRAY_EMPTY))
        End
    });

    return Success(ParkStatus, IO_UNPARK_OK);
}

IOPtr Console__heap_new()
{
    IOPtr result = v_ptr(Kernel__alloc(sizeof(IO)), IO);

    *result = (IO) {
        .read = Console__read,
        .write = Console__write,
        .open = Console__open,
        .close = Console__close,
        .park = Console__park,
        .unpark = Console__unpark,
        .server_write_buffer = Console__server_write_buffer,
        .data_consumed = Console__data_consumed,
    };

    return result;
}

extern ConsoleState console_state;

ConsoleStatePtr ConsoleState__instance()
{
    return &console_state;
}
