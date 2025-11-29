#include "console.h"
#include "debug.h"
#include "../../../types/result.h"
#include "../../../types/kstring.h"
#include "scheduler/scheduler.h"
#include "subsystems/io/io_subsystem.h"

FixedArrayBodyDefine(ParkedContexts);

static Result(IOInternalHandle) Console__open(IOStatePtr state, KString path, Flags flags)
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
    IOBuffer__reset(&self->read_buffer);
    IOBuffer__reset(&self->write_buffer);

    FindResultMatch(ParkedContexts, ParkedContexts__find(&self->parked_contexts, handle_match, &handle))
        IsSome(found,
        {
            ResultMatch(ArrayCode, ParkedContexts__remove(&self->parked_contexts, *found.value))
                IsError(code, message, __d_println(PANIC, "no parked context found for handle"));
            End

        })
        IsNone(__d_println(PANIC, "no parked context found for handle"));
    End

    return Success(IOInternalHandle, handle);
}

static Result(IOBufferPtr) Console__read(IOStatePtr state, IOInternalHandle handle, size_t size)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    if (IOBuffer__item_count(&self->read_buffer) == 0)
    {
        return Error(IOBufferPtr, kstring("wait for IO"), IO_WAIT_FOR_DATA);
    }
    return Success(IOBufferPtr, &self->read_buffer);
}

static Result(ByteCount) Console__write(IOStatePtr state, IOInternalHandle handle, DataPtr data, size_t size)
{
    return Success(ByteCount, 0);
}

static Result(ParkStatus) Console__park(IOStatePtr state, IOContextPtr parked_context)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;

    ParkedContexts__push(&self->parked_contexts, parked_context);
    __d_println(IODEV, "parked console context\n");
    return Success(ParkStatus, IO_PARK_OK);
}

static Result(ParkStatus) Console__unpark(IOStatePtr state)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    ParkedContexts__pop(&self->parked_contexts);
    return Success(ParkStatus, IO_UNPARK_OK);
}

IOPtr Console__heap_new()
{
    IOPtr result = v_ptr(Kernel__alloc(sizeof(IOPtr)), IO);

    *result = (IO) {
        .read = Console__read,
        .write = Console__write,
        .open = Console__open,
        .close = Console__close,
        .park = Console__park,
        .unpark = Console__unpark,
    };

    return result;
}

extern ConsoleState console_state;

ConsoleStatePtr ConsoleState__instance()
{
    return &console_state;
}
