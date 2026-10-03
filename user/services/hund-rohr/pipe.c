#include "pipe.h"

#include "sys/mem.h"

FixedArrayBodyDefine(PipeObjects);
FixedArrayBodyDefine(PipeEndpoints);

bool PipeObject__equals(void * first, void * second)
{
    PipeObjectPtr left = first;
    PipeObjectPtr right = second;
    return left->id == right->id;
}

bool PipeEndpoint__equals(void * first, void * second)
{
    PipeEndpointPtr left = first;
    PipeEndpointPtr right = second;
    return left->handle == right->handle;
}

PipeService PipeService__new(void)
{
    return (PipeService){
        .pipes = PipeObjects__new(),
        .endpoints = PipeEndpoints__new(),
        .next_pipe = 1,
        .next_handle = IO_SESSION_HANDLE_BASE,
    };
}

static PipeObject PipeObject__new(PipeID id, uint8_t * storage)
{
    return (PipeObject){
        .id = id,
        .storage = storage,
        .bytes = ByteRing__new(storage, PIPE_CAPACITY),
        .readers = 1,
        .writers = 1,
    };
}

static PipeEndpoint PipeEndpoint__new(IOHandle handle, PipeID pipe, IORequestPtr request, PipeEndpointKind kind)
{
    return (PipeEndpoint){
        .handle = handle,
        .pipe = pipe,
        .session = request->client_session,
        .pid = request->client_pid,
        .kind = kind,
    };
}

static bool PipeObject__id_matches(PipeObjectPtr self, void * context)
{
    return self->id == *(PipeIDPtr)context;
}

static Optional(PipeObjectPtr) PipeService__find_pipe(PipeServicePtr self, PipeID id)
{
    match_find_result_ptr(
        PipeObjects__find(&self->pipes, PipeObject__id_matches, &id),
        some(pipe)
        {
            return Some(PipeObjectPtr, pipe);
        },
        none()
        {
            return None(PipeObjectPtr);
        }
    );
}

static bool PipeEndpoint__request_matches(PipeEndpointPtr self, void * context)
{
    IORequestPtr request = context;
    return self->handle == request->handle && self->session == request->client_session &&
           self->pid == request->client_pid;
}

static Optional(PipeEndpointPtr) PipeService__find_endpoint(PipeServicePtr self, IORequestPtr request)
{
    match_find_result_ptr(
        PipeEndpoints__find(&self->endpoints, PipeEndpoint__request_matches, request),
        some(endpoint)
        {
            return Some(PipeEndpointPtr, endpoint);
        },
        none()
        {
            return None(PipeEndpointPtr);
        }
    );
}

static bool PipeEndpoint__session_matches(PipeEndpointPtr self, void * context)
{
    return self->session == *(QueueConnectionIDPtr)context;
}

static bool PipeService__remove_pipe(PipeServicePtr self, PipeID id)
{
    PipeObject removed;
    match_optional(PipeService__find_pipe(self, id),
        some(pipe)
        {
            removed = *pipe;
        },
        none()
        {
            return false;
        }
    );
    match_result(
        PipeObjects__remove(&self->pipes, removed),
        success(value)
        {
            free(removed.storage);
            return true;
        },
        error(code, message)
        {
            return false;
        }
    );
}

static bool PipeService__remove_endpoint(PipeServicePtr self, PipeEndpoint endpoint)
{
    match_result(
        PipeEndpoints__remove(&self->endpoints, endpoint),
        success(value)
        {
            return true;
        },
        error(code, message)
        {
            return false;
        }
    );
}

static bool PipeService__close(PipeServicePtr self, PipeEndpoint endpoint)
{
    PipeID pipe_id = endpoint.pipe;
    if (!PipeService__remove_endpoint(self, endpoint))
    {
        return false;
    }
    match_optional(
        PipeService__find_pipe(self, pipe_id),
        some(pipe)
        {
            if (endpoint.kind == PIPE_ENDPOINT_READ && pipe->readers > 0)
            {
                pipe->readers--;
            }
            if (endpoint.kind == PIPE_ENDPOINT_WRITE && pipe->writers > 0)
            {
                pipe->writers--;
            }
            if (pipe->readers == 0 && pipe->writers == 0)
            {
                return PipeService__remove_pipe(self, pipe_id);
            }
            return true;
        },
        none()
        {
            return false;
        }
    );
}

void PipeService__release(PipeServicePtr self, QueueConnectionID session)
{
    while (true)
    {
        match_find_result(
            PipeEndpoints__find(&self->endpoints, PipeEndpoint__session_matches, &session),
            some(endpoint)
            {
                if (!PipeService__close(self, endpoint))
                {
                    return;
                }
            },
            none()
            {
                return;
            }
        );
    }
}

static IOReply PipeService__create(PipeServicePtr self, IORequestPtr request)
{
    Flags required_flags = IO_OPEN_READ | IO_OPEN_WRITE;
    if (!request->buffered || (request->flags & required_flags) != required_flags)
    {
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    if (PipeObjects__len(&self->pipes) >= PipeObjects__max_size ||
        PipeEndpoints__len(&self->endpoints) > PipeEndpoints__max_size - 2 || self->next_handle > INT64_MAX - 2 ||
        self->next_pipe == UINT64_MAX)
    {
        return IOReply__new(IO_REGISTER_FAILED, 0);
    }
    uint8_t * storage;
    match_result(
        mem_alloc_bytes(PIPE_CAPACITY),
        success(allocation)
        {
            storage = (uint8_t *)allocation;
        },
        error(code, message)
        {
            return IOReply__new(IO_REGISTER_FAILED, 0);
        }
    );
    PipeObject pipe = PipeObject__new(self->next_pipe++, storage);
    PipeEndpoint reader = PipeEndpoint__new(self->next_handle++, pipe.id, request, PIPE_ENDPOINT_READ);
    PipeEndpoint writer = PipeEndpoint__new(self->next_handle++, pipe.id, request, PIPE_ENDPOINT_WRITE);
    match_result(
        PipeObjects__push(&self->pipes, pipe),
        success_do_nothing(),
        error(code, message)
        {
            free(storage);
            return IOReply__new(IO_REGISTER_FAILED, 0);
        }
    );
    match_result(
        PipeEndpoints__push(&self->endpoints, reader),
        success_do_nothing(),
        error(code, message)
        {
            PipeService__remove_pipe(self, pipe.id);
            return IOReply__new(IO_REGISTER_FAILED, 0);
        }
    );
    match_result(
        PipeEndpoints__push(&self->endpoints, writer),
        success_do_nothing(),
        error(code, message)
        {
            bool reader_removed = PipeService__remove_endpoint(self, reader);
            bool pipe_removed = PipeService__remove_pipe(self, pipe.id);
            return IOReply__new(reader_removed && pipe_removed ? IO_REGISTER_FAILED : IO_PANIC, 0);
        }
    );
    IOReply reply = IOReply__new(IO_OK, 0);
    reply.pipe = IOPipe__new(reader.handle, writer.handle);
    return reply;
}

static IOReply PipeService__read(PipeObjectPtr pipe, IORequestPtr request)
{
    IOReply reply = IOReply__new(IO_OK, 0);
    if (request->count == 0)
    {
        return reply;
    }
    if (ByteRing__count(&pipe->bytes) == 0)
    {
        IOStatus status = (request->flags & IO_TRANSFER_NONBLOCK) != 0 ? IO_WOULD_BLOCK : IO_WAIT_FOR_DATA;
        return IOReply__new(pipe->writers == 0 ? IO_EOF : status, 0);
    }
    reply.value = ByteRing__drain(&pipe->bytes, IORequest__read_target(request, &reply), request->count);
    return reply;
}

static IOReply PipeService__write(PipeObjectPtr pipe, IORequestPtr request)
{
    if (request->count == 0)
    {
        return IOReply__new(IO_OK, 0);
    }
    if (pipe->readers == 0)
    {
        return IOReply__new(IO_BROKEN_PIPE, 0);
    }
    size_t free_space = ByteRing__free_space(&pipe->bytes);
    if (free_space == 0 || (request->count <= PIPE_ATOMIC_WRITE && free_space < request->count))
    {
        return IOReply__new((request->flags & IO_TRANSFER_NONBLOCK) != 0 ? IO_WOULD_BLOCK : IO_WAIT_FOR_DATA, 0);
    }
    size_t count = request->count < free_space ? request->count : free_space;
    match_result(
        ByteRing__push(&pipe->bytes, IORequest__write_source(request), count),
        success(written)
        {
            return IOReply__new(IO_OK, written);
        },
        error(code, message)
        {
            return IOReply__new(IO_PANIC, 0);
        }
    );
}

IOReply PipeService__execute(PipeServicePtr self, IORequestPtr request)
{
    if (request->client_pid == 0 || request->client_session == 0 || request->count > IORequest__capacity(request))
    {
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    if (request->operation == IO_REQUEST_PIPE_CREATE)
    {
        return PipeService__create(self, request);
    }
    if ((request->operation == IO_REQUEST_READ || request->operation == IO_REQUEST_WRITE) &&
        (request->flags &
         ~(IO_TRANSFER_NONBLOCK | (request->operation == IO_REQUEST_WRITE ? IO_TRANSFER_APPEND : 0))) != 0)
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    PipeEndpointPtr endpoint;
    match_optional(
        PipeService__find_endpoint(self, request),
        some(found)
        {
            endpoint = found;
        },
        none()
        {
            return IOReply__new(IO_UNKNOWN_HANDLE, 0);
        }
    );
    if (request->operation == IO_REQUEST_CLOSE)
    {
        return IOReply__new(PipeService__close(self, *endpoint) ? IO_OK : IO_PANIC, 0);
    }
    PipeObjectPtr pipe;
    match_optional(
        PipeService__find_pipe(self, endpoint->pipe),
        some(found)
        {
            pipe = found;
        },
        none()
        {
            return IOReply__new(IO_UNKNOWN_HANDLE, 0);
        }
    );
    if (request->operation == IO_REQUEST_READ)
    {
        return endpoint->kind == PIPE_ENDPOINT_READ ? PipeService__read(pipe, request)
                                                    : IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    if (request->operation == IO_REQUEST_WRITE)
    {
        return endpoint->kind == PIPE_ENDPOINT_WRITE ? PipeService__write(pipe, request)
                                                     : IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
}
