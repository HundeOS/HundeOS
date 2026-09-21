#include "server.h"
#include "buffer/client.h"

FixedArrayBodyDefine(IOServiceConnections);

bool IOServiceConnection__equals(void * first, void * second)
{
    IOServiceConnectionPtr left = first;
    IOServiceConnectionPtr right = second;
    return ChannelServer__connection(&left->channel) == ChannelServer__connection(&right->channel);
}

static bool IOServiceConnection__matches(IOServiceConnectionPtr self, void * context)
{
    return ChannelServer__connection(&self->channel) == *(QueueConnectionIDPtr)context;
}

static IOServiceConnection IOServiceConnection__new(ChannelServer channel)
{
    return (IOServiceConnection){.channel = channel, .phase = IO_SERVICE_RECEIVE};
}

static IOService IOService__new(ConstBStringPtr name, QueueArrivalsPtr arrivals, IOServiceContext context,
                                IOServiceExecute execute, IOServiceDisconnect disconnect)
{
    return (IOService){.name = *name,
                       .arrivals = arrivals,
                       .connections = IOServiceConnections__new(),
                       .context = context,
                       .execute = execute,
                       .disconnect = disconnect};
}

IOStatus IOService__listen_into(IOServicePtr self, ConstBStringPtr name, IOServiceContext context,
                                IOServiceExecute execute, IOServiceDisconnect disconnect)
{
    match_result(
        Queue__listen(name),
        success(arrivals)
        {
            *self = IOService__new(name, arrivals, context, execute, disconnect);
            return IO_OK;
        },
        error(code, message)
        {
            return IO_REGISTER_FAILED;
        }
    );
}

Result(IOService) IOService__listen(ConstBStringPtr name, IOServiceContext context, IOServiceExecute execute,
                                    IOServiceDisconnect disconnect)
{
    match_result(
        Queue__listen(name),
        success(arrivals)
        {
            return Success(IOService, IOService__new(name, arrivals, context, execute, disconnect));
        },
        error(code, message)
        {
            return Error(IOService, message, code);
        }
    );
}

static bool IOService__accept(IOServicePtr self)
{
    uint64_t warden = unwrap_optional(
        ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr(CHANNEL_WARDEN_SERVICE_NAME)), 0);
    bool progress = false;
    for_each_atomic_fa_ptr(
        QueueConnectionSlots,
        &self->arrivals->connections,
        index,
        id,
        {
            if (id == 0)
            {
                continue;
            }
            bool exists = false;
            match_find_result(
                IOServiceConnections__find(&self->connections, IOServiceConnection__matches, &id),
                some(found)
                {
                    exists = true;
                },
                none_do_nothing()
            );
            if (exists)
            {
                continue;
            }
            match_result(
                ChannelServer__accept(id, warden),
                success(channel)
                {
                    match_result(
                        IOServiceConnections__push(&self->connections, IOServiceConnection__new(channel)),
                        success_do_nothing(),
                        error(code, message)
                        {
                            ChannelServer__close(&channel);
                        }
                    );
                },
                error(code, message)
                {
                    QueueConnection__close(id);
                }
            );
            progress = true;
        }
    );
    return progress;
}

static IOStatus IOServiceConnection__prepare(IOServiceConnectionPtr self, ConstBStringPtr service)
{
    IORequestPtr request = &self->request;
    request->transfer_address = 0;
    uint64_t client = ChannelServer__transport_pid(&self->channel);
    if (request->operation == IO_REQUEST_OPEN && request->buffered)
    {
        if ((request->flags & (IO_OPEN_READ | IO_OPEN_BIND | IO_OPEN_LISTEN)) && self->buffers.read.id == 0)
        {
            match_result(
                BufferClient__acquire(service, client, BUFFER_READ),
                success(buffer)
                {
                    self->buffers.read = buffer;
                },
                error(code, message)
                {
                    return IO_UNKNOWN_BUFFER;
                }
            );
        }
        if ((request->flags & (IO_OPEN_WRITE | IO_OPEN_BIND | IO_OPEN_LISTEN)) && self->buffers.write.id == 0)
        {
            match_result(
                BufferClient__acquire(service, client, BUFFER_WRITE),
                success(buffer)
                {
                    self->buffers.write = buffer;
                },
                error(code, message)
                {
                    return IO_UNKNOWN_BUFFER;
                }
            );
        }
    }
    if (request->operation == IO_REQUEST_READ || request->operation == IO_REQUEST_WRITE)
    {
        if (!request->buffered)
        {
            return request->count <= IO_PROTOCOL_DATA_SIZE ? IO_OK : IO_UNKNOWN_BUFFER;
        }
        BufferDescriptor descriptor = request->operation == IO_REQUEST_READ ? self->buffers.read : self->buffers.write;
        if (descriptor.id == 0 || request->buffer != descriptor.id ||
            !BufferDescriptor__contains(&descriptor, 0, request->count))
        {
            return IO_UNKNOWN_BUFFER;
        }
        request->transfer_address = descriptor.address;
    }
    return IO_OK;
}

static bool IOService__serve(IOServicePtr self, IOServiceConnectionPtr connection)
{
    if (connection->phase != IO_SERVICE_RECEIVE)
    {
        match_result(
            ChannelServer__poll(&connection->channel),
            success_do_nothing(),
            error(code, message)
            {
                ChannelServer__close(&connection->channel);
                return true;
            }
        );
    }
    if (connection->phase == IO_SERVICE_RECEIVE)
    {
        match_result(
            ChannelServer__poll(&connection->channel),
            success(incoming)
            {
                match_optional(
                    incoming,
                    some(metadata)
                    {
                        if (metadata.size != sizeof(IORequest) ||
                            !ChannelServer__read_request(&connection->channel, (ChannelDataPtr)&connection->request,
                                                         sizeof(IORequest)))
                        {
                            connection->reply = IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
                            connection->phase = IO_SERVICE_REPLY;
                        }
                        else
                        {
                            uint64_t client = ChannelServer__transport_pid(&connection->channel);
                            if (client == QUEUE_KERNEL_PID)
                            {
                                client = connection->request.client_pid;
                            }
                            if (client == 0 || client == QUEUE_KERNEL_PID ||
                                (connection->client_pid != 0 && connection->client_pid != client))
                            {
                                connection->reply = IOReply__new(IO_PERMISSION_DENIED, 0);
                                connection->phase = IO_SERVICE_REPLY;
                            }
                            else
                            {
                                connection->client_pid = client;
                                Optional(RequestTicket) previous =
                                    ChannelRequestContext__swap(&Some(RequestTicket, connection->channel.ticket));
                                IOStatus status = IOServiceConnection__prepare(connection, &self->name);
                                ChannelRequestContext__swap(&previous);
                                connection->phase = status == IO_OK ? IO_SERVICE_EXECUTE : IO_SERVICE_REPLY;
                                connection->reply = IOReply__new(status, 0);
                            }
                        }
                    },
                    none()
                    {
                        return false;
                    }
                );
            },
            error(code, message)
            {
                ChannelServer__close(&connection->channel);
                return true;
            }
        );
    }
    if (connection->phase == IO_SERVICE_EXECUTE)
    {
        if (connection->request.operation == IO_REQUEST_RELEASE_BUFFERS)
        {
            Optional(RequestTicket) previous =
                ChannelRequestContext__swap(&Some(RequestTicket, connection->channel.ticket));
            BufferStatus status =
                BufferClient__release_pair(&connection->buffers, ChannelServer__transport_pid(&connection->channel));
            ChannelRequestContext__swap(&previous);
            connection->reply = IOReply__new(status == BUFFER_OK ? IO_OK : IO_UNKNOWN_BUFFER, 0);
            connection->phase = IO_SERVICE_REPLY;
        }
    }
    if (connection->phase == IO_SERVICE_EXECUTE)
    {
        Optional(RequestTicket) previous =
            ChannelRequestContext__swap(&Some(RequestTicket, connection->channel.ticket));
        Optional(IOReply) response = self->execute(self->context, ChannelServer__connection(&connection->channel),
                                                   connection->client_pid, &connection->request);
        ChannelRequestContext__swap(&previous);
        match_optional(
            response,
            some(reply)
            {
                connection->reply = reply;
                connection->reply.buffers = connection->buffers;
                connection->phase = IO_SERVICE_REPLY;
            },
            none()
            {
                return false;
            }
        );
    }
    match_result(
        ChannelServer__reply(&connection->channel, (ConstChannelDataPtr)&connection->reply, sizeof(IOReply)),
        success(code)
        {
            connection->phase = IO_SERVICE_RECEIVE;
            return true;
        },
        error(code, message)
        {
            if (code != CHANNEL_PENDING)
            {
                ChannelServer__close(&connection->channel);
            }
            return false;
        }
    );
}

bool IOService__poll(IOServicePtr self)
{
    bool progress = IOService__accept(self);
    for (ArrayIndex index = 0; index < IOServiceConnections__len(&self->connections);)
    {
        match_optional(
            IOServiceConnections__ptr_at(&self->connections, index),
            some(connection)
            {
                if (ChannelServer__status(&connection->channel) != QUEUE_OK)
                {
                    if (!self->disconnect(self->context, ChannelServer__connection(&connection->channel),
                                          connection->client_pid))
                    {
                        index++;
                        continue;
                    }
                    IOServiceConnection removed = *connection;
                    if (BufferClient__release_pair(&removed.buffers, ChannelServer__transport_pid(&removed.channel)) !=
                        BUFFER_OK)
                    {
                        index++;
                        continue;
                    }
                    ChannelServer__close(&removed.channel);
                    match_result(
                        IOServiceConnections__remove(&self->connections, removed),
                        success(code)
                        {
                            progress = true;
                            continue;
                        },
                        error(code, message)
                        {
                            return progress;
                        }
                    );
                }
                progress = IOService__serve(self, connection) || progress;
            },
            none()
            {
                return progress;
            }
        );
        index++;
    }
    return progress;
}
