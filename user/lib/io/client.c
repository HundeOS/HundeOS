#include "client.h"
#include "buffer/client.h"

static Channel io_channel;
static bool connected;
static bool failed;
static BufferPair buffers;
static uint64_t open_handles;

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
            error(code, message)
            {
                return IOReply__new(IO_NO_DEVICE_FOUND, 0);
            }
        );
    }
    IOReply reply;
    match_result(
        Channel__submit(&io_channel, (ConstChannelDataPtr)request, sizeof(*request), (ChannelDataPtr)&reply,
                        sizeof(reply)),
        success(size)
        {
            if (size == sizeof(reply))
            {
                if (reply.status == IO_OK && request->operation == IO_REQUEST_OPEN)
                {
                    uint64_t peer = Channel__server_pid(&io_channel);
                    if ((reply.buffers.read.id != 0 &&
                         !BufferClient__validate(&reply.buffers.read, peer, BUFFER_READ)) ||
                        (reply.buffers.write.id != 0 &&
                         !BufferClient__validate(&reply.buffers.write, peer, BUFFER_WRITE)))
                    {
                        return IOReply__new(IO_UNKNOWN_BUFFER, 0);
                    }
                    buffers = reply.buffers;
                }
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
    request.buffered = true;
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
    if (reply.value < IO_SESSION_HANDLE_BASE)
    {
        return IO_UNKNOWN_HANDLE;
    }
    open_handles++;
    return (IOHandle)reply.value;
}

IOStatus IOClient__link(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    if (BString__length(source_path) >= IO_PROTOCOL_DATA_SIZE || BString__length(target_path) >= IO_PROTOCOL_DATA_SIZE)
    {
        return IO_INVALID_PATH;
    }
    IORequest request = IORequest__new(IO_REQUEST_LINK, 0);
    request.count = BString__copy_to(source_path, (char *)request.data, sizeof(request.data));
    IORequest__set_target_path(&request, target_path);
    return IOClient__submit(&request).status;
}

IOHandle IOClient__accept(IOHandle handle)
{
    IORequest request = IORequest__new(IO_REQUEST_ACCEPT, handle);
    IOReply reply = IOClient__submit(&request);
    if (reply.status != IO_OK)
    {
        return reply.status;
    }
    if (reply.value < IO_SESSION_HANDLE_BASE)
    {
        return IO_UNKNOWN_HANDLE;
    }
    open_handles++;
    return (IOHandle)reply.value;
}

IOStatus IOClient__close(IOHandle handle)
{
    IORequest request = IORequest__new(IO_REQUEST_CLOSE, handle);
    IOStatus status = IOClient__submit(&request).status;
    if (status == IO_OK && open_handles > 0 && --open_handles == 0)
    {
        request = IORequest__new(IO_REQUEST_RELEASE_BUFFERS, 0);
        status = IOClient__submit(&request).status;
        if (status == IO_OK)
        {
            buffers = BufferPair__new();
        }
    }
    return status;
}

IOStatus IOClient__set_foreground_process(IOHandle handle, uint64_t pid)
{
    IORequest request = IORequest__new(IO_REQUEST_SET_FOREGROUND_PROCESS, handle);
    request.foreground_pid = pid;
    return IOClient__submit(&request).status;
}

static ByteCount IOClient__read_chunk(IOHandle handle, ChannelDataPtr data, size_t size)
{
    IORequest request = IORequest__new(IO_REQUEST_READ, handle);
    if (buffers.read.id == 0)
    {
        return IO_UNKNOWN_BUFFER;
    }
    request.buffered = true;
    request.buffer = buffers.read.id;
    request.count = size < BUFFER_SIZE ? size : BUFFER_SIZE;
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
        __builtin_memcpy(data, (ConstChannelDataPtr)buffers.read.address, reply.value);
    }
    return (ByteCount)reply.value;
}

static ByteCount IOClient__write_chunk(IOHandle handle, ConstChannelDataPtr data, size_t size)
{
    IORequest request = IORequest__new(IO_REQUEST_WRITE, handle);
    if (buffers.write.id == 0)
    {
        return IO_UNKNOWN_BUFFER;
    }
    request.buffered = true;
    request.buffer = buffers.write.id;
    request.count = size < BUFFER_SIZE ? size : BUFFER_SIZE;
    if (request.count > 0)
    {
        __builtin_memcpy((ChannelDataPtr)buffers.write.address, data, request.count);
    }
    IOReply reply = IOClient__submit(&request);
    if (reply.status != IO_OK)
    {
        return reply.status;
    }
    return reply.value <= request.count ? (ByteCount)reply.value : IO_UNKNOWN_BUFFER;
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
        if (handle < IO_FILESYSTEM_HANDLE_BASE || handle >= IO_STREAM_HANDLE_BASE)
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
