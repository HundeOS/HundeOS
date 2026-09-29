#include "stream_registry.h"
#include "path.h"

LinkedListBodyDefine(StreamDevices);

bool StreamDevice__equals(void * first, void * second)
{
    StreamDevicePtr left = first;
    StreamDevicePtr right = second;
    BString left_name = IORoute__path(&left->route);
    BString right_name = IORoute__path(&right->route);
    return left->access == right->access && BString__equals(&left_name, &right_name);
}

StreamRegistry StreamRegistry__new(void)
{
    return (StreamRegistry){.devices = StreamDevices__new()};
}

static StreamDevice StreamDevice__new(ConstBStringPtr name, Flags access, ConstBStringPtr service)
{
    return (StreamDevice){.route = IORoute__new(service, name, &empty_bstr(), true), .access = access};
}

static bool StreamDevice__matches(StreamDevicePtr self, void * context)
{
    return StreamDevice__equals(self, context);
}

Result(IOStatus)
    StreamRegistry__register(StreamRegistryPtr self, ConstBStringPtr name, Flags access, ConstBStringPtr service)
{
    if (!IOPath__is_stream(name) || BString__is_empty(service) || (access != IO_OPEN_READ && access != IO_OPEN_WRITE))
    {
        return Error(IOStatus, bstr("Invalid stream registration"), IO_INVALID_PATH);
    }
    StreamDevice device = StreamDevice__new(name, access, service);
    match_find_result(
        StreamDevices__find(&self->devices, StreamDevice__matches, &device),
        some(found)
        {
            return Error(IOStatus, bstr("Stream already registered"), IO_REGISTER_FAILED);
        },
        none_do_nothing()
    );
    match_result(
        StreamDevices__push(&self->devices, device),
        success(code)
        {
            return Success(IOStatus, IO_OK);
        },
        error(code, message)
        {
            return Error(IOStatus, message, IO_REGISTER_FAILED);
        }
    );
}

static bool StreamDevice__named(StreamDevicePtr self, void * context)
{
    BString name = IORoute__path(&self->route);
    return BString__equals(&name, context);
}

IOReply StreamRegistry__route(StreamRegistryPtr self, IORequestPtr request)
{
    if (request->operation != IO_REQUEST_OPEN)
    {
        return IOReply__new(IO_UNKNOWN_HANDLE, 0);
    }
    BString name = IORequest__path(request);
    if (request->count > IO_PROTOCOL_DATA_SIZE || !IOPath__is_stream(&name))
    {
        return IOReply__new(IO_INVALID_PATH, 0);
    }
    match_find_result(
        StreamDevices__find(&self->devices, StreamDevice__named, &name),
        some(found)
        {
        },
        none()
        {
            return IOReply__new(IO_NO_DEVICE_FOUND, 0);
        }
    );
    StreamDevice key = StreamDevice__new(&name, request->flags, &empty_bstr());
    match_find_result(
        StreamDevices__find(&self->devices, StreamDevice__matches, &key),
        some(device)
        {
            IOReply reply = IOReply__new(IO_REDIRECT, 0);
            reply.route = device.route;
            return reply;
        },
        none()
        {
            return IOReply__new(IO_PERMISSION_DENIED, 0);
        }
    );
}

void StreamRegistry__dispose(StreamRegistryPtr self)
{
    StreamDevices__dispose(&self->devices);
}
