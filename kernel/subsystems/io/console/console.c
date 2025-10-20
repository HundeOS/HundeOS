#include "console.h"
#include "../io.h"
#include "../../../types/optional.h"
#include "../../../types/result.h"
#include "../../../types/kstring.h"
#include "subsystems/io/io_subsystem.h"

static Buffer buffer = NEW_BUFFER;

static Result(IOHandle) Console__open(IOStatePtr state, KString path, Flags flags)
{
    return Success(IOHandle, 1);
}

static Result(IOHandle) Console__close(IOStatePtr state, IOHandle handle)
{
    return Success(IOHandle, 1);
}

static Result(BufferPtr) Console__read(IOStatePtr state, IOHandle handle, size_t size)
{
    return Success(BufferPtr, &buffer);
}

static Result(ByteCount) Console__write(IOStatePtr state, IOHandle handle, BufferPtr buffer, size_t size)
{
    return Success(ByteCount, 0);
}

IOPtr Console__heap_new()
{
    IOPtr result = v_ptr(Kernel__alloc(sizeof(IOPtr)), IO);

    *result = (IO) {
        .read = Console__read,
        .write = Console__write,
        .open = Console__open,
        .close = Console__close,
    };

    return result;
}

ConsoleStatePtr ConsoleState__heap_new()
{
    ConsoleStatePtr state_ptr = v_ptr(Kernel__alloc(sizeof(ConsoleState)), ConsoleState);

    *state_ptr = (ConsoleState){
        .state = NEW_IO_STATE,
        .console_buffer = NEW_BUFFER,
    };
    return state_ptr;
}
