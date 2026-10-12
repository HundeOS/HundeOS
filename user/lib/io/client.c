#include "client.h"
#include "buffer/client.h"
#include "sys/queue_wait.h"
#include "types/fixed_array.h"

#define IO_CLIENT_SESSION_LIMIT 24U
#define IO_CLIENT_HANDLE_LIMIT 128U

typedef struct IOClientSession
{
    uint64_t service_length;
    uint8_t service_data[IO_PROTOCOL_DATA_SIZE];
    Channel channel;
    Channel watch_channel;
    BufferPair buffers;
    uint64_t open_handles;
    bool failed;
    bool watch_connected;
    bool watch_pending;
} IOClientSession;

typedef IOClientSession * IOClientSessionPtr;
OptionalDef(IOClientSession);
OptionalDef(IOClientSessionPtr);

static BString IOClientSession__service(IOClientSessionPtr self)
{
    return BString__from_chars((const char *)self->service_data, self->service_length);
}

static IOClientSession IOClientSession__new(ConstBStringPtr service, Channel channel)
{
    IOClientSession self = {.channel = channel, .buffers = BufferPair__new()};
    self.service_length = BString__copy_to(service, (char *)self.service_data, sizeof(self.service_data));
    return self;
}

static bool IOClientSession__equals(void * first, void * second)
{
    IOClientSessionPtr left = first;
    IOClientSessionPtr right = second;
    BString left_service = IOClientSession__service(left);
    BString right_service = IOClientSession__service(right);
    return BString__equals(&left_service, &right_service);
}

FixedArrayDefine(IOClientSession, IO_CLIENT_SESSION_LIMIT, IOClientSessions, IOClientSession__equals);
FixedArrayBodyDefine(IOClientSessions);

typedef struct IOClientHandle
{
    IOHandle local;
    IOHandle backend;
    uint64_t service_length;
    uint8_t service_data[IO_PROTOCOL_DATA_SIZE];
    bool single_transfer;
} IOClientHandle;

typedef IOClientHandle * IOClientHandlePtr;
OptionalDef(IOClientHandle);
OptionalDef(IOClientHandlePtr);

static BString IOClientHandle__service(IOClientHandlePtr self)
{
    return BString__from_chars((const char *)self->service_data, self->service_length);
}

static IOClientHandle IOClientHandle__new(IOHandle local, IOHandle backend, ConstBStringPtr service,
                                          bool single_transfer)
{
    IOClientHandle self = {.local = local, .backend = backend, .single_transfer = single_transfer};
    self.service_length = BString__copy_to(service, (char *)self.service_data, sizeof(self.service_data));
    return self;
}

static bool IOClientHandle__equals(void * first, void * second)
{
    IOClientHandlePtr left = first;
    IOClientHandlePtr right = second;
    return left->local == right->local;
}

FixedArrayDefine(IOClientHandle, IO_CLIENT_HANDLE_LIMIT, IOClientHandles, IOClientHandle__equals);
FixedArrayBodyDefine(IOClientHandles);

typedef struct IOClientExchange
{
    IOReply reply;
    IORoute route;
} IOClientExchange;

static IOClientExchange IOClientExchange__new(IOReply reply, IORoute route)
{
    return (IOClientExchange){.reply = reply, .route = route};
}

static IOClientExchange IOClientExchange__error(IOStatus status)
{
    IORoute route = IORoute__new(&empty_bstr(), &empty_bstr(), &empty_bstr(), false);
    return IOClientExchange__new(IOReply__new(status, 0), route);
}

static IOClientSessions sessions = EmptyArray(IOClientSessions);
static IOClientHandles handles = EmptyArray(IOClientHandles);
static IOHandle next_handle = IO_SESSION_HANDLE_BASE;

static BString IOClientInheritance__service(IOClientInheritance * self)
{
    return BString__from_chars((const char *)self->service_data, self->service_length);
}

static bool IOClientSession__named(IOClientSessionPtr self, void * context)
{
    BString service = IOClientSession__service(self);
    return BString__equals(&service, context);
}

static Optional(IOClientSessionPtr) IOClient__find_session(ConstBStringPtr service)
{
    match_find_result_ptr(
        IOClientSessions__find(&sessions, IOClientSession__named, (BStringPtr)service),
        some(session)
        {
            return Some(IOClientSessionPtr, session);
        },
        none()
        {
            return None(IOClientSessionPtr);
        }
    );
}

static Optional(IOClientSessionPtr) IOClient__session(ConstBStringPtr service)
{
    match_optional(
        IOClient__find_session(service),
        some(session)
        {
            return Some(IOClientSessionPtr, session);
        },
        none_do_nothing()
    );

    match_result(
        Channel__open(ServiceDirectory__shared(), service),
        success(channel)
        {
            IOClientSession session = IOClientSession__new(service, channel);
            match_result(
                IOClientSessions__push(&sessions, session),
                success_do_nothing(),
                error(code, message)
                {
                    Channel__close(&channel);
                    return None(IOClientSessionPtr);
                }
            );
        },
        error(code, message)
        {
            return None(IOClientSessionPtr);
        }
    );
    return IOClient__find_session(service);
}

static IOReply IOClient__submit_to(IOClientSessionPtr session, IORequestPtr request)
{
    if (session->failed)
    {
        return IOReply__new(IO_NO_DEVICE_FOUND, 0);
    }
    IOReply reply;
    match_result(
        Channel__submit(&session->channel, (ConstChannelDataPtr)request, sizeof(*request),
                        (ChannelDataPtr)&reply, sizeof(reply)),
        success(size)
        {
            if (size == sizeof(reply))
            {
                return reply;
            }
        },
        error(code, message)
        {
            if (code == CHANNEL_INTERRUPTED)
            {
                return IOReply__new(IO_INTERRUPTED, 0);
            }
            session->failed = true;
            Channel__close(&session->channel);
            return IOReply__new(IO_NO_DEVICE_FOUND, 0);
        }
    );
    session->failed = true;
    Channel__close(&session->channel);
    return IOReply__new(IO_NO_DEVICE_FOUND, 0);
}

static IOClientExchange IOClient__path_exchange(IORequestPtr request)
{
    IORoute route = IORoute__new(&bstr(IO_SYSTEM_SERVICE), &empty_bstr(), &empty_bstr(), false);
    bool final = false;
    for (uint64_t redirects = 0; redirects <= IO_ROUTE_LIMIT; redirects++)
    {
        BString service = IORoute__service(&route);
        Optional(IOClientSessionPtr) found = IOClient__session(&service);
        IOClientSessionPtr session;
        match_optional(
            found,
            some(value)
            {
                session = value;
            },
            none()
            {
                return IOClientExchange__error(IO_NO_DEVICE_FOUND);
            }
        );

        request->buffered = final;
        IOReply reply = IOClient__submit_to(session, request);
        if (reply.status != IO_REDIRECT)
        {
            return IOClientExchange__new(reply, route);
        }
        if (redirects == IO_ROUTE_LIMIT)
        {
            return IOClientExchange__error(IO_OPERATION_NOT_SUPPORTED);
        }

        BString next_service = IORoute__service(&reply.route);
        BString path = IORoute__path(&reply.route);
        BString target_path = IORoute__target_path(&reply.route);
        if (BString__is_empty(&next_service) || BString__length(&path) >= IO_PROTOCOL_DATA_SIZE ||
            BString__length(&target_path) >= IO_PROTOCOL_DATA_SIZE)
        {
            return IOClientExchange__error(IO_INVALID_PATH);
        }
        route = reply.route;
        request->count = BString__copy_to(&path, (char *)request->data, sizeof(request->data));
        IORequest__set_target_path(request, &target_path);
        if (request->operation == IO_REQUEST_LIST_DIR)
        {
            request->offset = reply.value;
        }
        final = route.final;
    }
    return IOClientExchange__error(IO_OPERATION_NOT_SUPPORTED);
}

static bool IOClientHandle__matches(IOClientHandlePtr self, void * context)
{
    return self->local == *(IOHandlePtr)context;
}

static Optional(IOClientHandlePtr) IOClient__find_handle(IOHandle handle)
{
    match_find_result_ptr(
        IOClientHandles__find(&handles, IOClientHandle__matches, &handle),
        some(found)
        {
            return Some(IOClientHandlePtr, found);
        },
        none()
        {
            return None(IOClientHandlePtr);
        }
    );
}

static IOReply IOClient__submit_handle(IOHandle handle, IORequestPtr request)
{
    IOClientHandlePtr resource;
    match_optional(
        IOClient__find_handle(handle),
        some(found)
        {
            resource = found;
        },
        none()
        {
            return IOReply__new(IO_UNKNOWN_HANDLE, 0);
        }
    );
    IOClientSessionPtr session;
    BString service = IOClientHandle__service(resource);
    match_optional(
        IOClient__find_session(&service),
        some(found)
        {
            session = found;
        },
        none()
        {
            return IOReply__new(IO_NO_DEVICE_FOUND, 0);
        }
    );
    request->handle = resource->backend;
    request->buffered = true;
    return IOClient__submit_to(session, request);
}

static IOClientInheritance IOClientInheritance__new(IOHandle local, uint64_t token, Flags flags, bool single_transfer,
                                                    ConstBStringPtr service)
{
    IOClientInheritance self = {.local = local, .token = token, .flags = flags, .single_transfer = single_transfer};
    self.service_length = BString__copy_to(service, (char *)self.service_data, sizeof(self.service_data));
    return self;
}

Result(IOClientInheritance) IOClient__prepare_inheritance(IOHandle handle)
{
    IOClientHandlePtr resource;
    match_optional(
        IOClient__find_handle(handle),
        some(found)
        {
            resource = found;
        },
        none()
        {
            return Error(IOClientInheritance, bstr("Unknown inherited handle"), IO_UNKNOWN_HANDLE);
        }
    );
    IORequest request = IORequest__new(IO_REQUEST_INHERIT_PREPARE, resource->backend);
    IOReply reply = IOClient__submit_handle(handle, &request);
    if (reply.status != IO_OK || reply.value == 0)
    {
        return Error(IOClientInheritance, bstr("Handle does not support inheritance"), reply.status);
    }
    BString service = IOClientHandle__service(resource);
    IOClientInheritance inheritance =
        IOClientInheritance__new(handle, reply.value, reply.inheritance_flags, resource->single_transfer, &service);
    return Success(IOClientInheritance, inheritance);
}

static IOStatus IOClient__inheritance_request(IOClientInheritancePtr inheritance, IORequestOperation operation,
                                              uint64_t child_pid)
{
    BString service = IOClientInheritance__service(inheritance);
    IOClientSessionPtr session;
    match_optional(
        IOClient__find_session(&service),
        some(found)
        {
            session = found;
        },
        none()
        {
            return IO_NO_DEVICE_FOUND;
        }
    );
    IORequest request = IORequest__new(operation, 0);
    request.inheritance_token = inheritance->token;
    request.inheritance_pid = child_pid;
    return IOClient__submit_to(session, &request).status;
}

IOStatus IOClient__commit_inheritance(IOClientInheritance * inheritance, uint64_t child_pid)
{
    return IOClient__inheritance_request(inheritance, IO_REQUEST_INHERIT_COMMIT, child_pid);
}

IOStatus IOClient__abort_inheritance(IOClientInheritance * inheritance)
{
    return IOClient__inheritance_request(inheritance, IO_REQUEST_INHERIT_ABORT, 0);
}

void IOClient__reset_after_fork(void)
{
    sessions = EmptyArray(IOClientSessions);
    handles = EmptyArray(IOClientHandles);
    QueueWait__reset_after_fork();
}

IOStatus IOClient__claim_inheritance(IOClientInheritance * inheritance)
{
    BString service = IOClientInheritance__service(inheritance);
    IOClientSessionPtr session;
    match_optional(
        IOClient__session(&service),
        some(found)
        {
            session = found;
        },
        none()
        {
            return IO_NO_DEVICE_FOUND;
        }
    );
    IORequest request = IORequest__new(IO_REQUEST_INHERIT_CLAIM, 0);
    request.inheritance_token = inheritance->token;
    request.flags = inheritance->flags;
    request.buffered = true;
    IOReply reply = IOClient__submit_to(session, &request);
    if (reply.status != IO_OK || reply.value < IO_SESSION_HANDLE_BASE)
    {
        return reply.status == IO_OK ? IO_UNKNOWN_HANDLE : reply.status;
    }
    uint64_t peer = Channel__server_pid(&session->channel);
    if ((reply.buffers.read.id != 0 && !BufferClient__validate(&reply.buffers.read, peer, BUFFER_READ)) ||
        (reply.buffers.write.id != 0 && !BufferClient__validate(&reply.buffers.write, peer, BUFFER_WRITE)))
    {
        return IO_UNKNOWN_BUFFER;
    }
    session->buffers = reply.buffers;
    IOClientHandle resource =
        IOClientHandle__new(inheritance->local, (IOHandle)reply.value, &service, inheritance->single_transfer);
    match_result(
        IOClientHandles__push(&handles, resource),
        success(code)
        {
            session->open_handles++;
            return IO_OK;
        },
        error(code, message)
        {
            return IO_REGISTER_FAILED;
        }
    );
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
    return IOClient__path_exchange(&request).reply;
}

IOHandle IOClient__open(ConstBStringPtr path, Flags flags)
{
    IORequest request = IORequest__new(IO_REQUEST_OPEN, 0);
    if (BString__length(path) >= IO_PROTOCOL_DATA_SIZE || next_handle == INT64_MAX)
    {
        return IO_INVALID_PATH;
    }
    request.count = BString__copy_to(path, (char *)request.data, sizeof(request.data));
    request.flags = flags;
    IOClientExchange exchange = IOClient__path_exchange(&request);
    if (exchange.reply.status != IO_OK)
    {
        return exchange.reply.status;
    }
    if (exchange.reply.value < IO_SESSION_HANDLE_BASE)
    {
        return IO_UNKNOWN_HANDLE;
    }

    IOClientSessionPtr session;
    BString service = IORoute__service(&exchange.route);
    match_optional(
        IOClient__find_session(&service),
        some(found)
        {
            session = found;
        },
        none()
        {
            return IO_NO_DEVICE_FOUND;
        }
    );
    uint64_t peer = Channel__server_pid(&session->channel);
    if ((exchange.reply.buffers.read.id != 0 &&
         !BufferClient__validate(&exchange.reply.buffers.read, peer, BUFFER_READ)) ||
        (exchange.reply.buffers.write.id != 0 &&
         !BufferClient__validate(&exchange.reply.buffers.write, peer, BUFFER_WRITE)))
    {
        return IO_UNKNOWN_BUFFER;
    }
    session->buffers = exchange.reply.buffers;
    IOClientHandle resource = IOClientHandle__new(next_handle++, (IOHandle)exchange.reply.value, &service,
                                                  !BString__starts_with(path, &bstr("/")));
    match_result(
        IOClientHandles__push(&handles, resource),
        success(code)
        {
            session->open_handles++;
            return resource.local;
        },
        error(code, message)
        {
            return IO_REGISTER_FAILED;
        }
    );
}

static void IOClient__discard_pipe(IOClientSessionPtr session, IOPipe pipe)
{
    IORequest close_reader = IORequest__new(IO_REQUEST_CLOSE, pipe.read);
    IORequest close_writer = IORequest__new(IO_REQUEST_CLOSE, pipe.write);
    IORequest release = IORequest__new(IO_REQUEST_RELEASE_BUFFERS, 0);
    IOClient__submit_to(session, &close_reader);
    IOClient__submit_to(session, &close_writer);
    if (session->open_handles == 0)
    {
        IOClient__submit_to(session, &release);
        session->buffers = BufferPair__new();
    }
}

Result(IOPipe) IOClient__pipe(void)
{
    if (IOClientHandles__len(&handles) > IOClientHandles__max_size - 2 || next_handle > INT64_MAX - 1)
    {
        return Error(IOPipe, bstr("No client handles available for pipe"), IO_REGISTER_FAILED);
    }
    IORequest request = IORequest__new(IO_REQUEST_PIPE_CREATE, 0);
    request.flags = IO_OPEN_READ | IO_OPEN_WRITE;
    IOClientExchange exchange = IOClient__path_exchange(&request);
    if (exchange.reply.status != IO_OK)
    {
        return Error(IOPipe, bstr("Pipe service rejected creation"), exchange.reply.status);
    }
    BString service = IORoute__service(&exchange.route);
    IOClientSessionPtr session;
    match_optional(
        IOClient__find_session(&service),
        some(found)
        {
            session = found;
        },
        none()
        {
            return Error(IOPipe, bstr("Pipe service session unavailable"), IO_NO_DEVICE_FOUND);
        }
    );
    if (exchange.reply.pipe.read < IO_SESSION_HANDLE_BASE || exchange.reply.pipe.write < IO_SESSION_HANDLE_BASE)
    {
        IOClient__discard_pipe(session, exchange.reply.pipe);
        return Error(IOPipe, bstr("Pipe service returned invalid handles"), IO_UNKNOWN_HANDLE);
    }
    uint64_t peer = Channel__server_pid(&session->channel);
    if (exchange.reply.buffers.read.id == 0 || exchange.reply.buffers.write.id == 0 ||
        !BufferClient__validate(&exchange.reply.buffers.read, peer, BUFFER_READ) ||
        !BufferClient__validate(&exchange.reply.buffers.write, peer, BUFFER_WRITE))
    {
        IOClient__discard_pipe(session, exchange.reply.pipe);
        return Error(IOPipe, bstr("Pipe service returned invalid buffers"), IO_UNKNOWN_BUFFER);
    }
    session->buffers = exchange.reply.buffers;

    IOClientHandle reader = IOClientHandle__new(next_handle++, exchange.reply.pipe.read, &service, true);
    IOClientHandle writer = IOClientHandle__new(next_handle++, exchange.reply.pipe.write, &service, true);
    match_result(
        IOClientHandles__push(&handles, reader),
        success_do_nothing(),
        error(code, message)
        {
            IOClient__discard_pipe(session, exchange.reply.pipe);
            return Error(IOPipe, bstr("Failed to register pipe reader"), IO_REGISTER_FAILED);
        }
    );
    match_result(
        IOClientHandles__push(&handles, writer),
        success_do_nothing(),
        error(code, message)
        {
            IOClientHandles__remove(&handles, reader);
            IOClient__discard_pipe(session, exchange.reply.pipe);
            return Error(IOPipe, bstr("Failed to register pipe writer"), IO_REGISTER_FAILED);
        }
    );
    session->open_handles += 2;
    return Success(IOPipe, IOPipe__new(reader.local, writer.local));
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
    return IOClient__path_exchange(&request).reply.status;
}

IOHandle IOClient__accept(IOHandle handle)
{
    IOClientHandlePtr listener;
    match_optional(
        IOClient__find_handle(handle),
        some(found)
        {
            listener = found;
        },
        none()
        {
            return IO_UNKNOWN_HANDLE;
        }
    );
    IORequest request = IORequest__new(IO_REQUEST_ACCEPT, listener->backend);
    IOReply reply = IOClient__submit_handle(handle, &request);
    if (reply.status != IO_OK)
    {
        return reply.status;
    }
    BString service = IOClientHandle__service(listener);
    IOClientHandle resource = IOClientHandle__new(next_handle++, (IOHandle)reply.value, &service, true);
    match_result(
        IOClientHandles__push(&handles, resource),
        success(code)
        {
            match_optional(
                IOClient__find_session(&service),
                some(session)
                {
                    session->open_handles++;
                },
                none_do_nothing()
            );
            return resource.local;
        },
        error(code, message)
        {
            return IO_REGISTER_FAILED;
        }
    );
}

IOStatus IOClient__close(IOHandle handle)
{
    IOClientHandle resource;
    match_optional(
        IOClient__find_handle(handle),
        some(found)
        {
            resource = *found;
        },
        none()
        {
            return IO_UNKNOWN_HANDLE;
        }
    );
    IORequest request = IORequest__new(IO_REQUEST_CLOSE, resource.backend);
    IOReply reply = IOClient__submit_handle(handle, &request);
    if (reply.status != IO_OK)
    {
        return reply.status;
    }
    match_result(
        IOClientHandles__remove(&handles, resource),
        success_do_nothing(),
        error(code, message)
        {
            return IO_UNKNOWN_HANDLE;
        }
    );
    BString service = IOClientHandle__service(&resource);
    match_optional(
        IOClient__find_session(&service),
        some(session)
        {
            if (session->open_handles > 0 && --session->open_handles == 0)
            {
                IORequest release = IORequest__new(IO_REQUEST_RELEASE_BUFFERS, 0);
                IOStatus status = IOClient__submit_to(session, &release).status;
                if (status != IO_OK)
                {
                    return status;
                }
                session->buffers = BufferPair__new();
            }
        },
        none()
        {
            return IO_NO_DEVICE_FOUND;
        }
    );
    return IO_OK;
}

IOStatus IOClient__set_foreground_process(IOHandle handle, uint64_t pid)
{
    IORequest request = IORequest__new(IO_REQUEST_SET_FOREGROUND_PROCESS, handle);
    request.foreground_pid = pid;
    return IOClient__submit_handle(handle, &request).status;
}

IOTerminalAttributesResult IOClient__get_terminal_attributes(IOHandle handle)
{
    IORequest request = IORequest__new(IO_REQUEST_GET_TERMINAL_ATTRIBUTES, handle);
    IOReply reply = IOClient__submit_handle(handle, &request);
    return IOTerminalAttributesResult__new(reply.status, reply.terminal_attributes);
}

IOStatus IOClient__set_terminal_attributes(IOHandle handle, IOTerminalAttributes attributes)
{
    IORequest request = IORequest__new(IO_REQUEST_SET_TERMINAL_ATTRIBUTES, handle);
    request.terminal_attributes = attributes;
    return IOClient__submit_handle(handle, &request).status;
}

IOReply IOClient__seek(IOHandle handle, int64_t offset, IOSeekWhence whence)
{
    IORequest request = IORequest__new(IO_REQUEST_SEEK, handle);
    request.offset = (uint64_t)offset;
    request.flags = whence;
    return IOClient__submit_handle(handle, &request);
}

IOReply IOClient__watch_file(IOHandle handle)
{
    IORequest request = IORequest__new(IO_REQUEST_WATCH_FILE, handle);
    IOReply reply = IOClient__submit_handle(handle, &request);
    if (reply.status == IO_OK)
    {
        reply.file_change.handle = handle;
    }
    return reply;
}

IOReply IOClient__wait_file_change(IOFileWatch * watches, size_t count)
{
    if (count == 0 || count > IO_FILE_WATCH_CAPACITY)
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    for (size_t index = 0; index < count; index++)
    {
        match_optional(
            IOClient__find_handle(watches[index].handle),
            some_do_nothing(),
            none()
            {
                return IOReply__new(IO_UNKNOWN_HANDLE, 0);
            }
        );
    }

    while (true)
    {
        for (ArrayIndex session_index = 0; session_index < IOClientSessions__len(&sessions); session_index++)
        {
            match_optional(
                IOClientSessions__ptr_at(&sessions, session_index),
                some(session)
                {
                    IORequest request = IORequest__new(IO_REQUEST_WAIT_FILE_CHANGE, 0);
                    for (size_t watch_index = 0; watch_index < count; watch_index++)
                    {
                        match_optional(
                            IOClient__find_handle(watches[watch_index].handle),
                            some(resource)
                            {
                                BString resource_service = IOClientHandle__service(resource);
                                BString session_service = IOClientSession__service(session);
                                if (BString__equals(&resource_service, &session_service))
                                {
                                    size_t request_index = request.count / sizeof(IOFileWatch);
                                    IORequest__file_watches(&request)[request_index] =
                                        IOFileWatch__new(resource->backend, watches[watch_index].generation);
                                    request.count += sizeof(IOFileWatch);
                                }
                            },
                            none_do_nothing()
                        );
                    }
                    if (request.count == 0 || session->watch_pending)
                    {
                        continue;
                    }
                    if (!session->watch_connected)
                    {
                        BString service = IOClientSession__service(session);
                        match_result(
                            Channel__open(ServiceDirectory__shared(), &service),
                            success(channel)
                            {
                                session->watch_channel = channel;
                                session->watch_connected = true;
                            },
                            error(code, message)
                            {
                                return IOReply__new(IO_NO_DEVICE_FOUND, 0);
                            }
                        );
                    }
                    match_result(
                        Channel__request(&session->watch_channel, (ConstChannelDataPtr)&request, sizeof(request)),
                        success(code)
                        {
                            session->watch_pending = true;
                        },
                        error(code, message)
                        {
                            session->watch_connected = false;
                            Channel__close(&session->watch_channel);
                            return IOReply__new(IO_NO_DEVICE_FOUND, 0);
                        }
                    );
                },
                none_do_nothing()
            );
        }

        Channel__prepare_wait();
        for (ArrayIndex session_index = 0; session_index < IOClientSessions__len(&sessions); session_index++)
        {
            match_optional(
                IOClientSessions__ptr_at(&sessions, session_index),
                some(session)
                {
                    if (!session->watch_pending)
                    {
                        continue;
                    }
                    IOReply reply;
                    match_result(
                        Channel__poll_reply_once(&session->watch_channel, (ChannelDataPtr)&reply, sizeof(reply)),
                        success(optional_size)
                        {
                            match_optional(
                                optional_size,
                                some(size)
                                {
                                    session->watch_pending = false;
                                    if (size != sizeof(reply) || reply.status != IO_OK)
                                    {
                                        return size == sizeof(reply) ? reply : IOReply__new(IO_NO_DEVICE_FOUND, 0);
                                    }
                                    IOHandle local = IO_UNKNOWN_HANDLE;
                                    for_each_fa(
                                        IOClientHandles,
                                        handles,
                                        handle_index,
                                        resource,
                                        {
                                            BString resource_service = IOClientHandle__service(&resource);
                                            BString session_service = IOClientSession__service(session);
                                            if (BString__equals(&resource_service, &session_service) &&
                                                resource.backend == reply.file_change.handle)
                                            {
                                                local = resource.local;
                                                break;
                                            }
                                        }
                                    );
                                    if (local == IO_UNKNOWN_HANDLE)
                                    {
                                        continue;
                                    }
                                    for (size_t watch_index = 0; watch_index < count; watch_index++)
                                    {
                                        if (watches[watch_index].handle == local)
                                        {
                                            reply.file_change.handle = local;
                                            return reply;
                                        }
                                    }
                                },
                                none_do_nothing()
                            );
                        },
                        error(code, message)
                        {
                            session->watch_pending = false;
                            session->watch_connected = false;
                            Channel__close(&session->watch_channel);
                            return IOReply__new(IO_NO_DEVICE_FOUND, 0);
                        }
                    );
                },
                none_do_nothing()
            );
        }
        Channel__wait_event();
    }
}

static ByteCount IOClient__read_chunk(IOClientHandlePtr resource, ChannelDataPtr data, size_t size, Flags flags)
{
    IOClientSessionPtr session;
    BString service = IOClientHandle__service(resource);
    match_optional(
        IOClient__find_session(&service),
        some(found)
        {
            session = found;
        },
        none()
        {
            return IO_NO_DEVICE_FOUND;
        }
    );
    if (session->buffers.read.id == 0)
    {
        return IO_UNKNOWN_BUFFER;
    }
    IORequest request = IORequest__new(IO_REQUEST_READ, resource->backend);
    request.buffered = true;
    request.buffer = session->buffers.read.id;
    request.count = size < BUFFER_SIZE ? size : BUFFER_SIZE;
    request.flags = flags;
    IOReply reply = IOClient__submit_to(session, &request);
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
        __builtin_memcpy(data, (ConstChannelDataPtr)session->buffers.read.address, reply.value);
    }
    return (ByteCount)reply.value;
}

static ByteCount IOClient__write_chunk(IOClientHandlePtr resource, ConstChannelDataPtr data, size_t size, Flags flags)
{
    IOClientSessionPtr session;
    BString service = IOClientHandle__service(resource);
    match_optional(
        IOClient__find_session(&service),
        some(found)
        {
            session = found;
        },
        none()
        {
            return IO_NO_DEVICE_FOUND;
        }
    );
    if (session->buffers.write.id == 0)
    {
        return IO_UNKNOWN_BUFFER;
    }
    IORequest request = IORequest__new(IO_REQUEST_WRITE, resource->backend);
    request.buffered = true;
    request.buffer = session->buffers.write.id;
    request.count = size < BUFFER_SIZE ? size : BUFFER_SIZE;
    request.flags = flags;
    if (request.count > 0)
    {
        __builtin_memcpy((ChannelDataPtr)session->buffers.write.address, data, request.count);
    }
    IOReply reply = IOClient__submit_to(session, &request);
    if (reply.status != IO_OK)
    {
        return reply.status;
    }
    return reply.value <= request.count ? (ByteCount)reply.value : IO_UNKNOWN_BUFFER;
}

ByteCount IOClient__read_with_flags(IOHandle handle, ChannelDataPtr data, size_t size, Flags flags)
{
    IOClientHandlePtr resource;
    match_optional(
        IOClient__find_handle(handle),
        some(found)
        {
            resource = found;
        },
        none()
        {
            return IO_UNKNOWN_HANDLE;
        }
    );
    size_t total = 0;
    do
    {
        ByteCount count = IOClient__read_chunk(resource, data + total, size - total, flags);
        if (count <= 0)
        {
            return total == 0 ? count : (ByteCount)total;
        }
        total += count;
        if (resource->single_transfer)
        {
            break;
        }
    } while (total < size);
    return total;
}

ByteCount IOClient__read(IOHandle handle, ChannelDataPtr data, size_t size)
{
    return IOClient__read_with_flags(handle, data, size, 0);
}

ByteCount IOClient__write_with_flags(IOHandle handle, ConstChannelDataPtr data, size_t size, Flags flags)
{
    IOClientHandlePtr resource;
    match_optional(
        IOClient__find_handle(handle),
        some(found)
        {
            resource = found;
        },
        none()
        {
            return IO_UNKNOWN_HANDLE;
        }
    );
    size_t total = 0;
    do
    {
        ByteCount count = IOClient__write_chunk(resource, data + total, size - total, flags);
        if (count <= 0)
        {
            return total == 0 ? count : (ByteCount)total;
        }
        total += count;
        if (resource->single_transfer)
        {
            break;
        }
    } while (total < size);
    return total;
}

ByteCount IOClient__write(IOHandle handle, ConstChannelDataPtr data, size_t size)
{
    return IOClient__write_with_flags(handle, data, size, 0);
}
