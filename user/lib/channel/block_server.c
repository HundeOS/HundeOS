#include "block_server.h"
#include "sys/service.h"

FixedArrayBodyDefine(BlockServerConnections);
_Static_assert(sizeof(BlockChannelRequest) < CHANNEL_PAYLOAD_CAPACITY, "Block request exceeds channel payload");
_Static_assert(sizeof(BlockChannelResponse) < CHANNEL_PAYLOAD_CAPACITY, "Block response exceeds channel payload");

bool BlockServerConnection__equals(void * first, void * second)
{
    BlockServerConnection * left = first;
    BlockServerConnection * right = second;
    return ChannelServer__connection(&left->channel) == ChannelServer__connection(&right->channel);
}

static BlockServerConnection BlockServerConnection__new(ChannelServer channel)
{
    return (BlockServerConnection){.channel = channel};
}

static BlockServer BlockServer__new(QueueArrivalsPtr arrivals, BlockProtocolBlockCount block_count,
                                    BlockServerContext context, BlockServerTransfer transfer)
{
    return (BlockServer){.arrivals = arrivals,
                         .connections = BlockServerConnections__new(),
                         .block_count = block_count,
                         .context = context,
                         .transfer = transfer};
}

Result(BlockServer) BlockServer__listen(ConstBStringPtr name, BlockProtocolBlockCount block_count,
                                        BlockServerContext context, BlockServerTransfer transfer)
{
    match_result(
        Queue__listen(name),
        success(arrivals) { return Success(BlockServer, BlockServer__new(arrivals, block_count, context, transfer)); },
        error(code, message) { return Error(BlockServer, message, code); }
    );
    return Error(BlockServer, bstr("Block listener unavailable"), CHANNEL_UNAVAILABLE);
}

static bool BlockServerConnection__matches(BlockServerConnection * self, void * context)
{
    return ChannelServer__connection(&self->channel) == *(QueueConnectionID *)context;
}

static bool BlockServer__accept(BlockServerPtr self)
{
    QueueParticipantID warden = unwrap_optional(
        ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr(CHANNEL_WARDEN_SERVICE_NAME)), 0);
    bool progress = false;
    for_each_atomic_fa_ptr(QueueConnectionSlots, &self->arrivals->connections, index, connection_id, {
        if (connection_id == 0)
        {
            continue;
        }
        bool exists = false;
        match_find_result(
            BlockServerConnections__find(&self->connections, BlockServerConnection__matches, &connection_id),
            some(found) { exists = true; },
            none_do_nothing()
        );
        if (exists)
        {
            continue;
        }
        match_result(
            ChannelServer__accept(connection_id, warden),
            success(channel)
            {
                match_result(
                    BlockServerConnections__push(&self->connections, BlockServerConnection__new(channel)),
                    success_do_nothing(),
                    error(code, message) { ChannelServer__close(&channel); }
                );
            },
            error(code, message) { QueueConnection__close(connection_id); }
        );
        progress = true;
    });
    return progress;
}

static BlockChannelResponse BlockServer__execute(BlockServerPtr self, BlockChannelRequest * request)
{
    if (request->operation == BLOCK_PROTOCOL_DESCRIBE)
    {
        return BlockChannelResponse__new(BLOCK_PROTOCOL_OK, self->block_count);
    }
    if ((request->operation != BLOCK_PROTOCOL_READ && request->operation != BLOCK_PROTOCOL_WRITE) ||
        request->block >= self->block_count)
    {
        return BlockChannelResponse__new(BLOCK_PROTOCOL_INVALID_ARGUMENT, 0);
    }
    BlockProtocolStatus status = self->transfer(self->context, request->operation, request->block, request->data);
    BlockChannelResponse response = BlockChannelResponse__new(status, status == BLOCK_PROTOCOL_OK ? 1 : 0);
    if (status == BLOCK_PROTOCOL_OK && request->operation == BLOCK_PROTOCOL_READ)
    {
        __builtin_memcpy(response.data, request->data, sizeof(response.data));
    }
    return response;
}

static bool BlockServer__serve(BlockServerPtr self, BlockServerConnection * connection)
{
    if (!connection->pending)
    {
        match_result(
            ChannelServer__poll(&connection->channel),
            success(request)
            {
                match_optional(
                    request,
                    some(metadata)
                    {
                        BlockChannelRequest message;
                        if (!connection->authorized && !service_ok(service_authorize_block_client(metadata.client_pid)))
                        {
                            connection->response = BlockChannelResponse__new(BLOCK_PROTOCOL_IO_ERROR, 0);
                        }
                        else if (metadata.size != sizeof(message) ||
                                 !ChannelServer__read_request(&connection->channel, (ChannelDataPtr)&message,
                                                              sizeof(message)))
                        {
                            connection->response = BlockChannelResponse__new(BLOCK_PROTOCOL_INVALID_ARGUMENT, 0);
                        }
                        else
                        {
                            connection->authorized = true;
                            connection->response = BlockServer__execute(self, &message);
                        }
                        connection->pending = true;
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
    match_result(
        ChannelServer__reply(&connection->channel, (ConstChannelDataPtr)&connection->response,
                             sizeof(connection->response)),
        success(code)
        {
            connection->pending = false;
            return true;
        },
        error(code, message)
        {
            if (code != CHANNEL_PENDING)
            {
                ChannelServer__close(&connection->channel);
                connection->pending = false;
            }
            return false;
        }
    );
    return false;
}

bool BlockServer__poll(BlockServerPtr self)
{
    bool progress = BlockServer__accept(self);
    for (ArrayIndex index = 0; index < BlockServerConnections__len(&self->connections);)
    {
        match_optional(
            BlockServerConnections__ptr_at(&self->connections, index),
            some(connection)
            {
                if (ChannelServer__status(&connection->channel) != QUEUE_OK)
                {
                    BlockServerConnection removed = *connection;
                    ChannelServer__close(&removed.channel);
                    match_result(
                        BlockServerConnections__remove(&self->connections, removed),
                        success(code)
                        {
                            progress = true;
                            continue;
                        },
                        error(code, message) { return progress; }
                    );
                }
                progress = BlockServer__serve(self, connection) || progress;
            },
            none() { return progress; }
        );
        index++;
    }
    return progress;
}
