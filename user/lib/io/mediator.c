#include "mediator.h"

FixedArrayBodyDefine(IOResources);
FixedArrayBodyDefine(IOPendingSessions);

bool IOPendingSession__equals(void * first, void * second)
{
    return *(QueueConnectionID *)first == *(QueueConnectionID *)second;
}

static bool IOMediator__opening(IOMediatorPtr self, QueueConnectionID session)
{
    match_optional(
        IOPendingSessions__find_item(&self->pending_opens, session),
        some(index) { return true; },
        none() { return false; }
    );
}

static void IOMediator__forget_open(IOMediatorPtr self, QueueConnectionID session)
{
    if (!IOMediator__opening(self, session))
    {
        return;
    }
    match_result(
        IOPendingSessions__remove(&self->pending_opens, session),
        success_do_nothing(),
        error(code, message)
        {
            self->failed = true;
            Channel__close(&self->backend);
        }
    );
}

bool IOResource__equals(void * first, void * second)
{
    IOResource * left = first;
    IOResource * right = second;
    return left->handle == right->handle;
}

IOMediator IOMediator__new(Channel backend)
{
    return (IOMediator){.backend = backend,
                        .resources = IOResources__new(),
                        .next_handle = IO_SESSION_HANDLE_BASE,
                        .handle_limit = IO_SESSION_HANDLE_BASE * 2,
                        .pending_opens = IOPendingSessions__new()};
}

IOMediator IOMediator__new_filesystem(Channel backend, IOHandle first_handle)
{
    IOMediator self = IOMediator__new(backend);
    self.filesystem = true;
    self.next_handle = first_handle;
    self.handle_limit = first_handle + IO_SESSION_HANDLE_BASE;
    return self;
}

static IOResource IOResource__new(IOHandle handle, IOHandle backend, QueueConnectionID session, uint64_t pid,
                                  Flags flags)
{
    return (IOResource){.handle = handle,
                        .backend = backend,
                        .session = session,
                        .pid = pid,
                        .flags = flags,
                        .listener = (flags & (IO_OPEN_BIND | IO_OPEN_LISTEN)) != 0};
}

static bool IOResource__matches(IOResource * self, void * context)
{
    return self->handle == *(IOHandle *)context;
}

static Optional(IOResource) IOMediator__find(IOMediatorPtr self, IOHandle handle)
{
    match_find_result(
        IOResources__find(&self->resources, IOResource__matches, &handle),
        some(resource) { return Some(IOResource, resource); },
        none() { return None(IOResource); }
    );
}

static IOStatus IOMediator__validate(IOMediatorPtr self, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    if (request->count > IO_PROTOCOL_DATA_SIZE)
    {
        return IO_OPERATION_NOT_SUPPORTED;
    }
    if (request->operation == IO_REQUEST_OPEN ||
        (self->filesystem && (request->operation == IO_REQUEST_LIST_DIR || request->operation == IO_REQUEST_MKDIR ||
                              request->operation == IO_REQUEST_REMOVE)))
    {
        BString path = IORequest__path(request);
        if (self->filesystem)
        {
            return BString__starts_with(&path, &bstr("/")) ? IO_OK : IO_INVALID_PATH;
        }
        return BString__starts_with(&path, &bstr(IO_SOCKET_SCHEME)) ? IO_OK : IO_INVALID_PATH;
    }
    IOResource resource;
    match_optional(
        IOMediator__find(self, request->handle),
        some(found) { resource = found; },
        none() { return IO_UNKNOWN_HANDLE; }
    );
    if (resource.session != session || resource.pid != pid)
    {
        return IO_UNKNOWN_HANDLE;
    }
    switch (request->operation)
    {
    case IO_REQUEST_ACCEPT:
        return !self->filesystem && resource.listener ? IO_OK : IO_OPERATION_NOT_SUPPORTED;
    case IO_REQUEST_READ:
        return resource.listener ? IO_OPERATION_NOT_SUPPORTED
                                 : ((resource.flags & IO_OPEN_READ) != 0 ? IO_OK : IO_PERMISSION_DENIED);
    case IO_REQUEST_WRITE:
        return resource.listener ? IO_OPERATION_NOT_SUPPORTED
                                 : ((resource.flags & IO_OPEN_WRITE) != 0 ? IO_OK : IO_PERMISSION_DENIED);
    case IO_REQUEST_CLOSE:
        return IO_OK;
    default:
        return IO_OPERATION_NOT_SUPPORTED;
    }
}

static Optional(IOReply) IOMediator__exchange(IOMediatorPtr self, QueueConnectionID session, IORequestPtr request)
{
    if (self->failed)
    {
        self->active = 0;
        return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0));
    }
    if (self->active != 0 && self->active != session)
    {
        return None(IOReply);
    }
    if (self->active == 0)
    {
        match_result(
            Channel__request(&self->backend, (ConstChannelDataPtr)request, sizeof(*request)),
            success(code)
            {
                self->active = session;
                self->active_operation = request->operation;
            },
            error(code, message)
            {
                self->failed = true;
                Channel__close(&self->backend);
                return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0));
            }
        );
    }
    IOReply reply;
    match_result(
        Channel__poll_reply(&self->backend, (ChannelDataPtr)&reply, sizeof(reply)),
        success(received)
        {
            match_optional(
                received,
                some(size)
                {
                    self->active = 0;
                    if (size != sizeof(reply))
                    {
                        self->failed = true;
                        Channel__close(&self->backend);
                        return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0));
                    }
                    return Some(IOReply, reply);
                },
                none() { return None(IOReply); }
            );
        },
        error(code, message)
        {
            self->active = 0;
            self->failed = true;
            Channel__close(&self->backend);
            return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0));
        }
    );
}

Optional(IOReply)
    IOMediator__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    IOMediatorPtr self = context;
    IOStatus status = IOMediator__validate(self, session, pid, request);
    if (status != IO_OK)
    {
        return Some(IOReply, IOReply__new(status, 0));
    }
    bool opens = request->operation == IO_REQUEST_OPEN || request->operation == IO_REQUEST_ACCEPT;
    if (opens && self->active != session && !IOMediator__opening(self, session) &&
        (IOResources__len(&self->resources) + IOPendingSessions__len(&self->pending_opens) >= IOResources__max_size ||
         self->next_handle == self->handle_limit))
    {
        return Some(IOReply, IOReply__new(IO_REGISTER_FAILED, 0));
    }
    IORequest forwarded = *request;
    forwarded.client_pid = pid;
    forwarded.client_session = session;
    match_optional(
        IOMediator__find(self, request->handle),
        some(resource) { forwarded.handle = resource.backend; },
        none_do_nothing()
    );
    IOReply reply;
    match_optional(
        IOMediator__exchange(self, session, &forwarded),
        some(received) { reply = received; },
        none() { return None(IOReply); }
    );
    if (reply.status == IO_WAIT_FOR_DATA)
    {
        if (opens && !IOMediator__opening(self, session))
        {
            match_result(
                IOPendingSessions__push(&self->pending_opens, session),
                success_do_nothing(),
                error(code, message)
                {
                    self->failed = true;
                    Channel__close(&self->backend);
                    return Some(IOReply, IOReply__new(IO_REGISTER_FAILED, 0));
                }
            );
        }
        return None(IOReply);
    }
    IOMediator__forget_open(self, session);
    if (reply.status != IO_OK)
    {
        return Some(IOReply, reply);
    }
    if ((opens && reply.value < IO_SESSION_HANDLE_BASE) ||
        ((request->operation == IO_REQUEST_READ || request->operation == IO_REQUEST_WRITE) &&
         (reply.value < 0 || (uint64_t)reply.value > request->count)))
    {
        self->failed = true;
        Channel__close(&self->backend);
        return Some(IOReply, IOReply__new(IO_UNKNOWN_BUFFER, 0));
    }
    if (opens)
    {
        IOResource resource =
            IOResource__new(self->next_handle++, reply.value, session, pid,
                            request->operation == IO_REQUEST_ACCEPT ? IO_OPEN_READ | IO_OPEN_WRITE : request->flags);
        match_result(
            IOResources__push(&self->resources, resource),
            success(code) { reply.value = resource.handle; },
            error(code, message)
            {
                self->failed = true;
                Channel__close(&self->backend);
                reply = IOReply__new(IO_REGISTER_FAILED, 0);
            }
        );
    }
    if (request->operation == IO_REQUEST_CLOSE)
    {
        match_optional(
            IOMediator__find(self, request->handle),
            some(resource)
            {
                match_result(
                    IOResources__remove(&self->resources, resource),
                    success_do_nothing(),
                    error(code, message) { reply = IOReply__new(IO_UNKNOWN_HANDLE, 0); }
                );
            },
            none_do_nothing()
        );
    }
    return Some(IOReply, reply);
}

static bool IOResource__session_matches(IOResource * self, void * context)
{
    return self->session == *(QueueConnectionID *)context;
}

bool IOMediator__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    IOMediatorPtr self = context;
    if (self->cleaning != 0 && self->cleaning != session)
    {
        return false;
    }
    bool owns_resources = false;
    match_find_result(
        IOResources__find(&self->resources, IOResource__session_matches, &session),
        some(resource) { owns_resources = true; },
        none_do_nothing()
    );
    if (!owns_resources && self->active != session && self->cleaning != session && !IOMediator__opening(self, session))
    {
        return true;
    }
    self->cleaning = session;
    IORequest request = IORequest__new(IO_REQUEST_RELEASE_CLIENT, 0);
    request.client_session = session;
    request.client_pid = pid;
    // Drain a departed client's operation before sending its resource release.
    if (self->active == session && self->active_operation != IO_REQUEST_RELEASE_CLIENT)
    {
        match_optional(
            IOMediator__exchange(self, session, &request),
            some(reply) { return false; },
            none() { return false; }
        );
    }
    if (pid != 0 && !self->failed)
    {
        match_optional(
            IOMediator__exchange(self, session, &request),
            some(reply)
            {
                if (reply.status == IO_WAIT_FOR_DATA)
                {
                    return false;
                }
                if (reply.status != IO_OK)
                {
                    self->failed = true;
                    Channel__close(&self->backend);
                }
            },
            none() { return false; }
        );
    }
    while (true)
    {
        match_find_result(
            IOResources__find(&self->resources, IOResource__session_matches, &session),
            some(resource)
            {
                match_result(
                    IOResources__remove(&self->resources, resource),
                    success_do_nothing(),
                    error(code, message) { return false; }
                );
            },
            none()
            {
                IOMediator__forget_open(self, session);
                self->cleaning = 0;
                return true;
            }
        );
    }
}
