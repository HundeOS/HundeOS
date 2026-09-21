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
ByteCount IOClient__write(IOHandle handle, ConstChannelDataPtr data, size_t size)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}
