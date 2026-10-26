#include "native.h"
#include "sys/io.h"

static HundeIOCompletion HundeIOCompletion__new(ByteCount count)
{
    return (HundeIOCompletion){.kind = HUNDE_ABI_SUCCESS, .value = count};
}

static HundeIOCompletion HundeIOCompletion__new_error(Code code)
{
    return (HundeIOCompletion){.kind = HUNDE_ABI_ERROR, .error = RustNative__error(code).error};
}

static Result(ByteCount) RustFile__read(IOHandle handle, uint8_t * buffer, size_t capacity)
{
    size_t offset = 0;
    while (offset < capacity)
    {
        match_result(
            IO__read_bytes(handle, (char *)buffer + offset, capacity - offset),
            success(count)
            {
                if (count < 0 || (uint64_t)count > capacity - offset)
                {
                    return Error(ByteCount, bstr("Invalid file read count"), IO_UNKNOWN_BUFFER);
                }
                if (count == 0)
                {
                    return Success(ByteCount, offset);
                }
                offset += count;
            },
            error(code, message)
            {
                return code == IO_EOF ? Success(ByteCount, offset) : Error(ByteCount, message, code);
            }
        );
    }
    char extra;
    match_result(
        IO__read_bytes(handle, &extra, 1),
        success(count)
        {
            if (count != 0)
            {
                return Error(ByteCount, bstr("File exceeds read limit"), IO_UNKNOWN_BUFFER);
            }
            return Success(ByteCount, offset);
        },
        error(code, message)
        {
            return code == IO_EOF ? Success(ByteCount, offset) : Error(ByteCount, message, code);
        }
    );
    return Error(ByteCount, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

HundeIOCompletion hunde_rust_read_file(HundeByteSpan path, uint8_t * buffer, size_t capacity)
{
    if (path.length == 0 || path.length >= IO_PROTOCOL_DATA_SIZE)
    {
        return HundeIOCompletion__new_error(IO_INVALID_PATH);
    }
    BString name = BString__from_chars((const char *)path.bytes, path.length);
    match_result(
        IO__open(&name, IO_OPEN_READ),
        success(handle)
        {
            Result(ByteCount) read = RustFile__read(handle, buffer, capacity);
            Result(IOStatus) closed = IO__close(handle);
            match_result(
                read,
                success(count)
                {
                    match_result(
                        closed,
                        success(status)
                        {
                            return HundeIOCompletion__new(count);
                        },
                        error(code, message)
                        {
                            return HundeIOCompletion__new_error(code);
                        }
                    );
                },
                error(code, message)
                {
                    return HundeIOCompletion__new_error(code);
                }
            );
        },
        error(code, message)
        {
            return HundeIOCompletion__new_error(code);
        }
    );
    return HundeIOCompletion__new_error(RESULT_UNKNOWN_ERROR_CODE);
}
