#include "sockets.h"
#include "sys/print.h"

bool SocketByte__equals(void * first, void * second)
{
    return *(SocketBytePtr)first == *(SocketBytePtr)second;
}

bool SocketID__equals(void * first, void * second)
{
    return *(SocketIDPtr)first == *(SocketIDPtr)second;
}

FixedArrayBodyDefine(SocketBytes);
FixedArrayBodyDefine(SocketPending);
FixedArrayBodyDefine(SocketResources);

bool SocketResource__equals(void * first, void * second)
{
    SocketResourcePtr left = first;
    SocketResourcePtr right = second;
    return left->id == right->id;
}

SocketService SocketService__new(void)
{
    return (SocketService){.resources = SocketResources__new(), .next_id = IO_SESSION_HANDLE_BASE};
}

static SocketResource SocketResource__new(SocketID id, IORequestPtr request, SocketKind kind)
{
    SocketResource self = {.id = id,
                           .session = request->client_session,
                           .pid = request->client_pid,
                           .kind = kind,
                           .flags = request->flags,
                           .peer = None(SocketID),
                           .pending = SocketPending__new(),
                           .bytes = SocketBytes__new()};
    BString name = IORequest__path(request);
    self.name_length = BString__copy_to(&name, (char *)self.name_data, sizeof(self.name_data));
    return self;
}

static bool SocketResource__id_matches(SocketResourcePtr self, void * context)
{
    return self->id == *(SocketIDPtr)context;
}

static Optional(SocketResourcePtr) SocketService__find(SocketServicePtr self, SocketID id)
{
    match_find_result_ptr(
        SocketResources__find(&self->resources, SocketResource__id_matches, &id),
        some(resource)
        {
            return Some(SocketResourcePtr, resource);
        },
        none()
        {
            return None(SocketResourcePtr);
        }
    );
}

static bool SocketResource__name_matches(SocketResourcePtr self, void * context)
{
    BString name = BString__from_chars((const char *)self->name_data, self->name_length);
    return self->kind == SOCKET_LISTENER && BString__equals(&name, context);
}

static bool SocketResource__session_matches(SocketResourcePtr self, void * context)
{
    return self->session == *(QueueConnectionIDPtr)context;
}

static void SocketService__close(SocketServicePtr self, SocketID id)
{
    SocketResource removed;
    match_optional(SocketService__find(self, id),
        some(resource)
        {
            removed = *resource;
        },
        none()
        {
            return;
        }
    );
    match_optional(
        removed.peer,
        some(peer_id)
        {
            match_optional(
                SocketService__find(self, peer_id),
                some(peer)
                {
                    peer->peer = None(SocketID);
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    match_result(
        SocketResources__remove(&self->resources, removed),
        success_do_nothing(),
        error(code, message)
        {
            IO__printf(&bstr("socket close failed %i\n"), code);
            return;
        }
    );
    for_each_fa(
        SocketPending,
        removed.pending,
        index,
        pending_id,
        {
            SocketService__close(self, pending_id);
        }
    );
}

void SocketService__release(SocketServicePtr self, QueueConnectionID session)
{
    while (true)
    {
        match_find_result(
            SocketResources__find(&self->resources, SocketResource__session_matches, &session),
            some(resource)
            {
                SocketService__close(self, resource.id);
            },
            none()
            {
                return;
            }
        );
    }
}

static IOReply SocketService__open(SocketServicePtr self, IORequestPtr request)
{
    BString path = IORequest__path(request);
    if (!BString__starts_with(&path, &bstr(IO_SOCKET_SCHEME)) ||
        BString__length(&path) <= BString__length(&bstr(IO_SOCKET_SCHEME)) ||
        (request->flags & ~(IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_BIND | IO_OPEN_LISTEN)) != 0)
    {
        return IOReply__new(IO_INVALID_PATH, 0);
    }

    Optional(SocketResourcePtr) listener_opt = None(SocketResourcePtr);
    match_find_result_ptr(
        SocketResources__find(&self->resources, SocketResource__name_matches, &path),
        some(listener)
        {
            listener_opt = Some(SocketResourcePtr, listener);
        },
        none_do_nothing()
    );
    if ((request->flags & (IO_OPEN_BIND | IO_OPEN_LISTEN)) != 0)
    {
        match_optional(listener_opt,
            some(found)
            {
                return IOReply__new(IO_REGISTER_FAILED, 0);
            },
            none_do_nothing()
        );
        if (self->next_id >= INT64_MAX)
        {
            return IOReply__new(IO_REGISTER_FAILED, 0);
        }
        SocketResource listener = SocketResource__new(self->next_id++, request, SOCKET_LISTENER);
        match_result(
            SocketResources__push(&self->resources, listener),
            success(code)
            {
                return IOReply__new(IO_OK, listener.id);
            },
            error(code, message)
            {
                return IOReply__new(IO_REGISTER_FAILED, 0);
            }
        );
    }
    SocketResourcePtr listener;
    match_optional(
        listener_opt,
        some(found)
        {
            listener = found;
        },
        none()
        {
            return IOReply__new(IO_NO_DEVICE_FOUND, 0);
        }
    );
    if (SocketPending__len(&listener->pending) >= SocketPending__max_size ||
        SocketResources__len(&self->resources) > SocketResources__max_size - 2 || self->next_id >= INT64_MAX - 1)
    {
        return IOReply__new(IO_REGISTER_FAILED, 0);
    }
    SocketResource client = SocketResource__new(self->next_id++, request, SOCKET_CONNECTION);
    SocketResource server = SocketResource__new(self->next_id++, request, SOCKET_CONNECTION);
    server.session = listener->session;
    server.pid = listener->pid;
    server.flags = IO_OPEN_READ | IO_OPEN_WRITE;
    client.peer = Some(SocketID, server.id);
    server.peer = Some(SocketID, client.id);
    match_result(
        SocketResources__push(&self->resources, client),
        success_do_nothing(),
        error(code, message)
        {
            return IOReply__new(IO_REGISTER_FAILED, 0);
        }
    );
    match_result(
        SocketResources__push(&self->resources, server),
        success_do_nothing(),
        error(code, message)
        {
            SocketService__close(self, client.id);
            return IOReply__new(IO_REGISTER_FAILED, 0);
        }
    );
    match_result(
        SocketPending__push(&listener->pending, server.id),
        success_do_nothing(),
        error(code, message)
        {
            SocketService__close(self, client.id);
            SocketService__close(self, server.id);
            return IOReply__new(IO_REGISTER_FAILED, 0);
        }
    );
    return IOReply__new(IO_OK, client.id);
}

static IOReply SocketService__read(SocketResourcePtr resource, IORequestPtr request)
{
    uint64_t count = request->count;
    if (resource->kind != SOCKET_CONNECTION)
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }

    IOReply reply = IOReply__new(IO_OK, 0);
    if (count == 0)
    {
        return reply;
    }
    if (SocketBytes__len(&resource->bytes) == 0)
    {
        match_optional(
            resource->peer,
            some(peer)
            {
                return IOReply__new(IO_WAIT_FOR_DATA, 0);
            },
            none()
            {
                return reply;
            }
        );
    }
    while (reply.value < count)
    {
        match_optional(
            SocketBytes__pop_front(&resource->bytes),
            some(byte)
            {
                IORequest__read_target(request, &reply)[reply.value++] = byte;
            },
            none()
            {
                break;
            }
        );
    }
    return reply;
}

static IOReply SocketService__write(SocketServicePtr self, SocketResourcePtr resource, IORequestPtr request)
{
    if (resource->kind != SOCKET_CONNECTION)
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }

    if (request->count == 0)
    {
        return IOReply__new(IO_OK, 0);
    }
    SocketID peer_id;
    match_optional(resource->peer,
        some(id)
        {
            peer_id = id;
        },
        none()
        {
            return IOReply__new(IO_NOT_OPEN, 0);
        }
    );
    SocketResourcePtr peer;
    match_optional(
        SocketService__find(self, peer_id),
        some(found)
        {
            peer = found;
        },
        none()
        {
            return IOReply__new(IO_NOT_OPEN, 0);
        }
    );
    uint64_t written = 0;
    while (written < request->count && SocketBytes__len(&peer->bytes) < SocketBytes__max_size)
    {
        match_result(
            SocketBytes__push(&peer->bytes, IORequest__write_source(request)[written]),
            success(code)
            {
                written++;
            },
            error(code, message)
            {
                return IOReply__new(IO_UNKNOWN_BUFFER, 0);
            }
        );
    }
    return IOReply__new(written == 0 ? IO_WAIT_FOR_DATA : IO_OK, written);
}

IOReply SocketService__execute(SocketServicePtr self, IORequestPtr request)
{
    if (request->client_pid == 0 || request->client_session == 0 || request->count > IORequest__capacity(request))
    {
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }

    if (request->operation == IO_REQUEST_RELEASE_CLIENT)
    {
        SocketService__release(self, request->client_session);
        return IOReply__new(IO_OK, 0);
    }
    if (request->operation == IO_REQUEST_OPEN)
    {
        return SocketService__open(self, request);
    }
    SocketResourcePtr resource;
    match_optional(
        SocketService__find(self, request->handle),
        some(found)
        {
            resource = found;
        },
        none()
        {
            return IOReply__new(IO_UNKNOWN_HANDLE, 0);
        }
    );
    if (resource->session != request->client_session || resource->pid != request->client_pid)
    {
        return IOReply__new(IO_UNKNOWN_HANDLE, 0);
    }
    switch (request->operation)
    {
    case IO_REQUEST_CLOSE:
        SocketService__close(self, resource->id);
        return IOReply__new(IO_OK, 0);
    case IO_REQUEST_ACCEPT:
        if (resource->kind != SOCKET_LISTENER)
        {
            return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
        }
        match_optional(
            SocketPending__pop_front(&resource->pending),
            some(id)
            {
                return IOReply__new(IO_OK, id);
            },
            none()
            {
                return IOReply__new(IO_WAIT_FOR_DATA, 0);
            }
        );
    case IO_REQUEST_READ:
        if (resource->kind == SOCKET_CONNECTION && (resource->flags & IO_OPEN_READ) == 0)
        {
            return IOReply__new(IO_PERMISSION_DENIED, 0);
        }
        return SocketService__read(resource, request);
    case IO_REQUEST_WRITE:
        if (resource->kind == SOCKET_CONNECTION && (resource->flags & IO_OPEN_WRITE) == 0)
        {
            return IOReply__new(IO_PERMISSION_DENIED, 0);
        }
        return SocketService__write(self, resource, request);
    default:
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
}
