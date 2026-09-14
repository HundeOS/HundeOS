#include "client.h"

static Channel io_channel;
static bool connected;
static bool failed;

static IOReply IOClient__submit(IORequestPtr request)
{
    if (failed)
    {
        return IOReply__new(IO_NO_DEVICE_FOUND, 0);
    }
    if (!connected)
    {
        match_result(
            Channel__open(ServiceDirectory__shared(), &bstr(IO_SYSTEM_SERVICE)),
            success(channel)
            {
                io_channel = channel;
                connected = true;
            },
            error(code, message) { return IOReply__new(IO_NO_DEVICE_FOUND, 0); }
        );
    }
    IOReply reply;
    match_result(
        Channel__submit(&io_channel, (ConstChannelDataPtr)request, sizeof(*request), (ChannelDataPtr)&reply,
                        sizeof(reply)),
        success(size)
        {
            if (size == sizeof(reply) && (reply.status != IO_OK || reply.value >= 0))
            {
                return reply;
            }
        },
        error(code, message)
        {
            failed = true;
            Channel__close(&io_channel);
            return IOReply__new(IO_NO_DEVICE_FOUND, 0);
        }
    );
    failed = true;
    Channel__close(&io_channel);
    return IOReply__new(IO_NO_DEVICE_FOUND, 0);
}

IOReply IOClient__path_request(IORequestOperation operation, ConstBStringPtr path, Flags flags, uint64_t offset)
{
    IORequest request = IORequest__new(operation, 0);
    if (BString__length(path) >= IO_PROTOCOL_DATA_SIZE)
    {
        return IOReply__new(IO_INVALID_PATH, 0);
    }
    request.count = BString__copy_to(path, (char *)request.data, sizeof(request.data));
    request.flags = flags;
    request.offset = offset;
    return IOClient__submit(&request);
}

IOHandle IOClient__open(ConstBStringPtr path, Flags flags)
{
    IOReply reply = IOClient__path_request(IO_REQUEST_OPEN, path, flags, 0);
    if (reply.status != IO_OK)
    {
        return reply.status;
    }
    return reply.value >= IO_SESSION_HANDLE_BASE ? reply.value : IO_UNKNOWN_HANDLE;
}

IOHandle IOClient__accept(IOHandle handle)
{
    IORequest request = IORequest__new(IO_REQUEST_ACCEPT, handle);
    IOReply reply = IOClient__submit(&request);
    if (reply.status != IO_OK)
    {
        return reply.status;
    }
    return reply.value >= IO_SESSION_HANDLE_BASE ? reply.value : IO_UNKNOWN_HANDLE;
}

IOStatus IOClient__close(IOHandle handle)
{
    IORequest request = IORequest__new(IO_REQUEST_CLOSE, handle);
    return IOClient__submit(&request).status;
}

static ByteCount IOClient__read_chunk(IOHandle handle, ChannelDataPtr data, size_t size)
{
    IORequest request = IORequest__new(IO_REQUEST_READ, handle);
    request.count = size < IO_PROTOCOL_DATA_SIZE ? size : IO_PROTOCOL_DATA_SIZE;
    IOReply reply = IOClient__submit(&request);
    if (reply.status != IO_OK)
    {
        return reply.status;
    }
    if ((uint64_t)reply.value > request.count)
    {
        return IO_UNKNOWN_BUFFER;
    }
    if (reply.value > 0)
    {
        __builtin_memcpy(data, reply.data, reply.value);
    }
    return reply.value;
}

static ByteCount IOClient__write_chunk(IOHandle handle, ConstChannelDataPtr data, size_t size)
{
    IORequest request = IORequest__new(IO_REQUEST_WRITE, handle);
    request.count = size < IO_PROTOCOL_DATA_SIZE ? size : IO_PROTOCOL_DATA_SIZE;
    if (request.count > 0)
    {
        __builtin_memcpy(request.data, data, request.count);
    }
    IOReply reply = IOClient__submit(&request);
    if (reply.status != IO_OK)
    {
        return reply.status;
    }
    return (uint64_t)reply.value <= request.count ? reply.value : IO_UNKNOWN_BUFFER;
}

ByteCount IOClient__read(IOHandle handle, ChannelDataPtr data, size_t size)
{
    size_t total = 0;
    do
    {
        ByteCount count = IOClient__read_chunk(handle, data + total, size - total);
        if (count <= 0)
        {
            return total == 0 ? count : (ByteCount)total;
        }
        total += count;
        if (handle < IO_FILESYSTEM_HANDLE_BASE)
        {
            break;
        }
    } while (total < size);
    return total;
}

ByteCount IOClient__write(IOHandle handle, ConstChannelDataPtr data, size_t size)
{
    size_t total = 0;
    do
    {
        ByteCount count = IOClient__write_chunk(handle, data + total, size - total);
        if (count <= 0)
        {
            return total == 0 ? count : (ByteCount)total;
        }
        total += count;
        if (handle < IO_FILESYSTEM_HANDLE_BASE)
        {
            break;
        }
    } while (total < size);
    return total;
}
