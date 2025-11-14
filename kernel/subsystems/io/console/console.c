#include "console.h"
#include "debug.h"
#include "../io.h"
#include "../../../types/result.h"
#include "../../../types/kstring.h"
#include "subsystems/io/io_subsystem.h"

static Result(IOInternalHandle) Console__open(IOStatePtr state, KString path, Flags flags)
{
    return Success(IOInternalHandle, 1);
}

static Result(IOInternalHandle) Console__close(IOStatePtr state, IOInternalHandle handle)
{
    return Success(IOInternalHandle, 1);
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

static Result(ParkStatus) Console__park(IOStatePtr state, IOContext parked_context)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    self->parked_context = Some(IOContext, parked_context);
    __d_println(IODEV, "parked console context\n");
    return Success(ParkStatus, IO_PARK_OK);
}

static Result(ParkStatus) Console__unpark(IOStatePtr state)
{
    ConsoleStatePtr self = (ConsoleStatePtr) state;
    self->parked_context = None(IOContext);
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

void Console__IRQ_handler(ConsoleStatePtr self, Char data)
{

}
