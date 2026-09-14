#include "server.h"

FixedArrayBodyDefine(IOServiceConnections);

bool IOServiceConnection__equals(void * first, void * second)
{
    IOServiceConnectionPtr left = first;
    IOServiceConnectionPtr right = second;
    return ChannelServer__connection(&left->channel) == ChannelServer__connection(&right->channel);
}

static bool IOServiceConnection__matches(IOServiceConnectionPtr self, void * context)
{
    return ChannelServer__connection(&self->channel) == *(QueueConnectionID *)context;
}

static IOServiceConnection IOServiceConnection__new(ChannelServer channel)
{
    return (IOServiceConnection){.channel = channel, .phase = IO_SERVICE_RECEIVE};
}

static IOService IOService__new(QueueArrivalsPtr arrivals, IOServiceContext context, IOServiceExecute execute,
                                IOServiceDisconnect disconnect)
{
    return (IOService){.arrivals = arrivals,
                       .connections = IOServiceConnections__new(),
                       .context = context,
                       .execute = execute,
                       .disconnect = disconnect};
}

Result(IOService) IOService__listen(ConstBStringPtr name, IOServiceContext context, IOServiceExecute execute,
                                    IOServiceDisconnect disconnect)
{
    match_result(
        Queue__listen(name),
        success(arrivals) { return Success(IOService, IOService__new(arrivals, context, execute, disconnect)); },
        error(code, message) { return Error(IOService, message, code); }
    );
}

static bool IOService__accept(IOServicePtr self)
{
    uint64_t warden = unwrap_optional(
        ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr(CHANNEL_WARDEN_SERVICE_NAME)), 0);
    bool progress = false;
    for_each_atomic_fa_ptr(QueueConnectionSlots, &self->arrivals->connections, index, id, {
        if (id == 0)
        {
            continue;
        }
        bool exists = false;
        match_find_result(
            IOServiceConnections__find(&self->connections, IOServiceConnection__matches, &id),
            some(found) { exists = true; },
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
                    error(code, message) { ChannelServer__close(&channel); }
                );
            },
            error(code, message) { QueueConnection__close(id); }
        );
        progress = true;
    });
    return progress;
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
                            connection->phase = IO_SERVICE_EXECUTE;
                        }
                    },
                    none() { return false; }
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
        match_optional(
            self->execute(self->context, ChannelServer__connection(&connection->channel),
                          ChannelServer__client_pid(&connection->channel), &connection->request),
            some(reply)
            {
                connection->reply = reply;
                connection->phase = IO_SERVICE_REPLY;
            },
            none() { return false; }
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
                                          ChannelServer__client_pid(&connection->channel)))
                    {
                        index++;
                        continue;
                    }
                    IOServiceConnection removed = *connection;
                    ChannelServer__close(&removed.channel);
                    match_result(
                        IOServiceConnections__remove(&self->connections, removed),
                        success(code)
                        {
                            progress = true;
                            continue;
                        },
                        error(code, message) { return progress; }
                    );
                }
                progress = IOService__serve(self, connection) || progress;
            },
            none() { return progress; }
        );
        index++;
    }
    return progress;
}
