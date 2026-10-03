#include "unity.h"
#include "io/client.h"

IOHandle IOClient__open(ConstBStringPtr path, Flags flags)
{
    if (!BString__starts_with(path, &bstr(IO_SOCKET_SCHEME)))
    {
        return IO_PATH_UNMANAGED;
    }
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}

Result(IOPipe) IOClient__pipe(void)
{
    TEST_FAIL_MESSAGE("Unexpected pipe IO");
    return Error(IOPipe, bstr("unexpected pipe"), IO_OPERATION_NOT_SUPPORTED);
}

IOStatus IOClient__link(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    TEST_FAIL_MESSAGE("Unexpected filesystem link");
    return IO_OPERATION_NOT_SUPPORTED;
}

IOReply IOClient__path_request(IORequestOperation operation, ConstBStringPtr path, Flags flags, uint64_t offset)
{
    return IOReply__new(IO_PATH_UNMANAGED, 0);
}

IOHandle IOClient__accept(IOHandle handle)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}

IOStatus IOClient__close(IOHandle handle)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return IO_UNKNOWN_HANDLE;
}

ByteCount IOClient__read(IOHandle handle, ChannelDataPtr data, size_t size)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}

ByteCount IOClient__read_with_flags(IOHandle handle, ChannelDataPtr data, size_t size, Flags flags)
{
    return IOClient__read(handle, data, size);
}

ByteCount IOClient__write(IOHandle handle, ConstChannelDataPtr data, size_t size)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}

ByteCount IOClient__write_with_flags(IOHandle handle, ConstChannelDataPtr data, size_t size, Flags flags)
{
    return IOClient__write(handle, data, size);
}
