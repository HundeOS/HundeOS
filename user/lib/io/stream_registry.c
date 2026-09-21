#include "stream_registry.h"
#include "path.h"

LinkedListBodyDefine(StreamDevices);
LinkedListBodyDefine(StreamBackends);

bool StreamDevice__equals(void * first, void * second)
{
    StreamDevicePtr left = first;
    StreamDevicePtr right = second;
    return left->access == right->access && BString__equals(&left->name, &right->name);
}

bool StreamBackend__equals(void * first, void * second)
{
    StreamBackendPtr left = first;
    StreamBackendPtr right = second;
    return BString__equals(&left->service, &right->service);
}

StreamRegistry StreamRegistry__new(void)
{
    return (StreamRegistry){
        .devices = StreamDevices__new(), .backends = StreamBackends__new(), .next_handle = IO_STREAM_HANDLE_BASE};
}

static StreamDevice StreamDevice__new(ConstBStringPtr name, Flags access, ConstBStringPtr service)
{
    return (StreamDevice){.name = *name, .access = access, .service = *service};
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
    return BString__equals(&self->name, context);
}

static bool StreamBackend__named(StreamBackendPtr self, void * context)
{
    return BString__equals(&self->service, context);
}

static bool StreamBackend__owns(StreamBackendPtr self, void * context)
{
    IOHandle handle = *(IOHandlePtr)context;
    return handle >= self->first_handle && handle < self->first_handle + IO_SESSION_HANDLE_BASE;
}

static StreamBackend StreamBackend__new(ConstBStringPtr service, Channel channel, IOHandle first_handle)
{
    return (StreamBackend){
        .service = *service, .mediator = IOMediator__new_stream(channel, first_handle), .first_handle = first_handle};
}

static Optional(IOReply) StreamRegistry__forward(StreamRegistryPtr self, ConstBStringPtr service,
                                                 QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    match_find_result_ptr(
        StreamBackends__find(&self->backends, StreamBackend__named, (BStringPtr)service),
        some(backend)
        {
            return IOMediator__execute(&backend->mediator, session, pid, request);
        },
        none_do_nothing()
    );
    if (self->next_handle > INT64_MAX - IO_SESSION_HANDLE_BASE)
    {
        return Some(IOReply, IOReply__new(IO_REGISTER_FAILED, 0));
    }
    match_result(
        Channel__open(ServiceDirectory__shared(), service),
        success(channel)
        {
            StreamBackend backend = StreamBackend__new(service, channel, self->next_handle);
            match_result(
                StreamBackends__push(&self->backends, backend),
                success(code)
                {
                    self->next_handle += IO_SESSION_HANDLE_BASE;
                },
                error(code, message)
                {
                    Channel__close(&channel);
                    return Some(IOReply, IOReply__new(IO_REGISTER_FAILED, 0));
                }
            );
        },
        error(code, message)
        {
            return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0));
        }
    );
    match_find_result_ptr(
        StreamBackends__find(&self->backends, StreamBackend__named, (BStringPtr)service),
        some(backend)
        {
            return IOMediator__execute(&backend->mediator, session, pid, request);
        },
        none()
        {
            return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0));
        }
    );
}

Optional(IOReply)
    StreamRegistry__execute(StreamRegistryPtr self, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    if (request->operation != IO_REQUEST_OPEN)
    {
        match_find_result_ptr(
            StreamBackends__find(&self->backends, StreamBackend__owns, &request->handle),
            some(backend)
            {
                return IOMediator__execute(&backend->mediator, session, pid, request);
            },
            none()
            {
                return Some(IOReply, IOReply__new(IO_UNKNOWN_HANDLE, 0));
            }
        );
    }
    BString name = IORequest__path(request);
    if (request->count > IO_PROTOCOL_DATA_SIZE || !IOPath__is_stream(&name))
    {
        return Some(IOReply, IOReply__new(IO_INVALID_PATH, 0));
    }
    match_find_result(
        StreamDevices__find(&self->devices, StreamDevice__named, &name),
        some(found)
        {
        },
        none()
        {
            return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0));
        }
    );
    StreamDevice key = StreamDevice__new(&name, request->flags, &empty_bstr());
    match_find_result(
        StreamDevices__find(&self->devices, StreamDevice__matches, &key),
        some(device)
        {
            return StreamRegistry__forward(self, &device.service, session, pid, request);
        },
        none()
        {
            return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
        }
    );
}

bool StreamRegistry__disconnect(StreamRegistryPtr self, QueueConnectionID session, uint64_t pid)
{
    bool complete = true;
    // The linked-list iteration macros copy values; acquire pointers to mutate mediators.
    for (ListIndex index = 0; index < StreamBackends__len(&self->backends); index++)
    {
        match_optional(
            StreamBackends__ptr_at(&self->backends, index),
            some(backend)
            {
                complete = IOMediator__disconnect(&backend->mediator, session, pid) && complete;
            },
            none()
            {
                return false;
            }
        );
    }
    return complete;
}

void StreamRegistry__dispose(StreamRegistryPtr self)
{
    for_each_ll(
        StreamBackends,
        self->backends,
        backend,
        {
            Channel__close(&backend.mediator.backend);
        }
    );
    StreamBackends__dispose(&self->backends);
    StreamDevices__dispose(&self->devices);
}
