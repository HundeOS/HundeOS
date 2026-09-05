#include "io.h"

#undef open
#undef read
#undef read_into
#undef write
#undef close

FixedArrayBodyDefine(DataBuffer);

static BString unreachable_code = bstr("unreachable code");

#define CallIOReturn(RT, context, block) \
    Result(RT) res = block; \
    IOContext__dispose(context); \
    return res;

// write sets the number of bytes (len) to be written
ExternalBuffer DataBuffer__convert_to_external_buffer_for_write(DataBufferPtr self)
{
    return ExternalBuffer__new(self->data, self->len);
}

// read sets the max size that can be read (buffer max size)
ExternalBuffer DataBuffer__convert_to_external_buffer_for_read(DataBufferPtr self)
{
    return ExternalBuffer__new(self->data, DataBuffer__max_size);
}

DataPtr DataBuffer__data(DataBufferPtr self)
{
    return self->data;
}

void DataBuffer__dispose(DataBufferPtr self)
{
    Kernel__free(addr(self));
}

Result(IOHandle) open(ConstOriginatingContextPtr context, ConstBStringPtr path, Flags flags)
{
    Optional(IOHandle) handle = None(IOHandle);
    Optional(ExternalBuffer) buffer = None(ExternalBuffer);
    IOContextPtr io_context = IOContext__from_originating_context(context, &handle, &None(ExternalBuffer));
    __d_printf(IOSUB, "Opening %s %p\n", cstr_p(path), context->kernel);

    CallIOReturn(IOHandle, io_context, IOSubsystem__open(context->kernel->io_subsystem, io_context, path, flags));
}

Result(ByteCount) read(ConstOriginatingContextPtr context, IOHandle handle, DataBufferPtr buffer)
{
    IOContextPtr io_context = IOContext__from_originating_context(
        context,
        &Some(IOHandle, handle),
        &Some(ExternalBuffer, DataBuffer__convert_to_external_buffer_for_read(buffer))
    );

    Result(ByteCount) res = IOSubsystem__read(context->kernel->io_subsystem, io_context);
    IOContext__dispose(io_context);
    match_result(ByteCount, res,
        success(bytes_read)
        {
            __d_printf(EXEC, "Read %i bytes\n", bytes_read);
            buffer->len = bytes_read;
            return Success(ByteCount, bytes_read);
        },
        error(code, message) { return Error(ByteCount, message, code); }
    );

    return Error(ByteCount, unreachable_code, IO_PANIC);
}

Result(ByteCount) read_into(ConstOriginatingContextPtr context, IOHandle handle, DataPtr target, size_t size)
{
    IOContextPtr io_context = IOContext__from_originating_context(
        context,
        &Some(IOHandle, handle),
        &Some(ExternalBuffer, ExternalBuffer__new(target, size))
    );

    Result(ByteCount) res = IOSubsystem__read(context->kernel->io_subsystem, io_context);
    IOContext__dispose(io_context);
    match_result(ByteCount, res,
        success(bytes_read)
        {
            __d_printf(EXEC, "Read %i bytes\n", bytes_read);
            return Success(ByteCount, bytes_read);
        },
        error(code, message) { return Error(ByteCount, message, code); }
    );

    return Error(ByteCount, unreachable_code, IO_PANIC);
}

Result(ByteCount) write(ConstOriginatingContextPtr context, IOHandle handle, DataBufferPtr buffer)
{
    IOContextPtr io_context = IOContext__from_originating_context(context, &Some(IOHandle, handle), &Some(ExternalBuffer, DataBuffer__convert_to_external_buffer_for_write(buffer)));

    CallIOReturn(ByteCount, io_context, IOSubsystem__write(context->kernel->io_subsystem, io_context));
}

Result(IOHandle) close(ConstOriginatingContextPtr context, IOHandle handle)
{
    IOContextPtr io_context = IOContext__from_originating_context(context, &Some(IOHandle, handle), &None(ExternalBuffer));

    CallIOReturn(IOHandle, io_context, IOSubsystem__close(context->kernel->io_subsystem, io_context));
}
