#include "console.h"
#include "../../../types/optional.h"
#include "../../../types/result.h"

typedef Result(IOHandle) (* IO_open)(KString path, Flags flags);
typedef Result(BufferPtr) (* IO_read)(IOHandle handle, size_t size);
typedef Result(BytesWritten) (* IO_write)(IOHandle handle, Optional(BufferPtr) buffer);


static Buffer buffer = NEW_BUFFER;

static Result(IOHandle) Console__open(KString path, Flags flags)
{
    return Success(IOHandle, 1);
}

static Result(BufferPtr) Console__read(IOHandle handle, size_t size)
{
    return Success(BufferPtr, &buffer);
}

static Result(BytesWritten) Console__write(IOHandle handle, Optional(BufferPtr) buffer)
{
    return Success(BytesWritten, 0);
}

const IO Console = {
    .read = Console__read,
    .write = Console__write,
    .open = Console__open,
};