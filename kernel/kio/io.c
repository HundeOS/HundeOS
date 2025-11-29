#include "io.h"

FixedArrayBodyDefine(DataBuffer);

#define CallIOReturn(RT, context, block) \
    Result(RT) res = block; \
    IOContext__dispose(context); \
    return res;

#define CallIOMatch(RT, context, block) \
    Result(RT) res = block; \
    IOContext__dispose(context); \
    ResultMatch(RT, res) \


// write sets the number of bytes (len) to be written
ExternalBuffer DataBuffer__convert_to_external_buffer_for_write(DataBufferPtr self)
{
    return (ExternalBuffer) {
        .data = self->data,
        .len = self->len,
    };
}

// read sets the max size that can be read (buffer max size)
ExternalBuffer DataBuffer__convert_to_external_buffer_for_read(DataBufferPtr self)
{
    return (ExternalBuffer) {
        .data = self->data,
        .len = DataBuffer__max_size,
    };
}

Result(IOHandle) open(OriginatingContext context, KString path, Flags flags)
{
    IOContextPtr io_context = IOContext__from_originating_context(context, None(IOHandle), None(ExternalBuffer));
    __d_printf(IOSUB, "Opening %s %p\n", cstr(path), context.kernel);

    CallIOReturn(IOHandle, io_context, IOSubsystem__open(context.kernel->io_subsystem, io_context, path, flags));
}

Result(ByteCount) read(OriginatingContext context, IOHandle handle, DataBufferPtr buffer)
{
    Optional(ExternalBuffer) external_buffer = Some(ExternalBuffer, DataBuffer__convert_to_external_buffer_for_read(buffer));
    IOContextPtr io_context = IOContext__from_originating_context(context, Some(IOHandle, handle), external_buffer);

    CallIOMatch(ByteCount, io_context, IOSubsystem__read(context.kernel->io_subsystem, io_context))
        IsSuccess(bytes_read,
        {
            __d_printf(EXEC, "Read %i bytes\n", bytes_read);
            buffer->len = bytes_read;
            return Success(ByteCount, bytes_read);
        });
        IsError(code, message, return Error(ByteCount, message, code));
    End
}

Result(ByteCount) write(OriginatingContext context, IOHandle handle, DataBufferPtr buffer)
{
    Optional(ExternalBuffer) external_buffer = Some(ExternalBuffer, DataBuffer__convert_to_external_buffer_for_write(buffer));
    IOContextPtr io_context = IOContext__from_originating_context(context, Some(IOHandle, handle), external_buffer);

    CallIOReturn(ByteCount, io_context, IOSubsystem__write(context.kernel->io_subsystem, io_context));
}

Result(IOHandle) close(OriginatingContext context, IOHandle handle)
{
    IOContextPtr io_context = IOContext__from_originating_context(context, Some(IOHandle, handle), None(ExternalBuffer));

    CallIOReturn(IOHandle, io_context, IOSubsystem__close(context.kernel->io_subsystem, io_context));
}