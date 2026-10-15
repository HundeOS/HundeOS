#include "block_server.h"
#include "sys/service.h"
#include "buffer/client.h"

FixedArrayBodyDefine(BlockServerConnections);
_Static_assert(sizeof(BlockChannelRequest) < CHANNEL_PAYLOAD_CAPACITY, "Block request exceeds channel payload");
_Static_assert(sizeof(BlockChannelResponse) < CHANNEL_PAYLOAD_CAPACITY, "Block response exceeds channel payload");

bool BlockServerConnection__equals(void * first, void * second)
{
    BlockServerConnectionPtr left = first;
    BlockServerConnectionPtr right = second;
    return ChannelServer__connection(&left->channel) == ChannelServer__connection(&right->channel);
}

static BlockServerConnection BlockServerConnection__new(ChannelServer channel)
{
    return (BlockServerConnection){.channel = channel};
}

static BlockServer BlockServer__new(ConstBStringPtr name, QueueArrivalsPtr arrivals,
                                    BlockProtocolBlockCount block_count, BlockServerContext context,
                                    BlockServerTransfer transfer)
{
    return (BlockServer){.name = *name,
                         .arrivals = arrivals,
                         .connections = BlockServerConnections__new(),
                         .block_count = block_count,
                         .context = context,
                         .transfer = transfer};
}

ChannelCode BlockServer__listen_into(BlockServerPtr self, ConstBStringPtr name, BlockProtocolBlockCount block_count,
                                     BlockServerContext context, BlockServerTransfer transfer)
{
    match_result(
        Queue__listen(name),
        success(arrivals)
        {
            *self = BlockServer__new(name, arrivals, block_count, context, transfer);
            return CHANNEL_OK;
        },
        error(code, message)
        {
            return CHANNEL_UNAVAILABLE;
        }
    );
    return CHANNEL_UNAVAILABLE;
}

Result(BlockServer) BlockServer__listen(ConstBStringPtr name, BlockProtocolBlockCount block_count,
                                        BlockServerContext context, BlockServerTransfer transfer)
{
    match_result(
        Queue__listen(name),
        success(arrivals)
        {
            return Success(BlockServer, BlockServer__new(name, arrivals, block_count, context, transfer));
        },
        error(code, message)
        {
            return Error(BlockServer, message, code);
        }
    );
    return Error(BlockServer, bstr("Block listener unavailable"), CHANNEL_UNAVAILABLE);
}

static bool BlockServerConnection__matches(BlockServerConnectionPtr self, void * context)
{
    return ChannelServer__connection(&self->channel) == *(QueueConnectionIDPtr)context;
}

static bool BlockServer__accept(BlockServerPtr self)
{
    QueueParticipantID warden = unwrap_optional(
        ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr(CHANNEL_WARDEN_SERVICE_NAME)), 0);
    bool progress = false;
    for_each_atomic_fa_ptr(
        QueueConnectionSlots,
        &self->arrivals->connections,
        index,
        connection_id,
        {
            if (connection_id == 0)
            {
                continue;
            }
            bool exists = false;
            match_find_result(
                BlockServerConnections__find(&self->connections, BlockServerConnection__matches, &connection_id),
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
                ChannelServer__accept(connection_id),
                success(channel)
                {
                    match_result(
                        BlockServerConnections__push(&self->connections, BlockServerConnection__new(channel)),
                        success_do_nothing(),
                        error(code, message)
                        {
                            ChannelServer__close(&channel);
                        }
                    );
                },
                error(code, message)
                {
                    QueueConnection__close(connection_id);
                }
            );
            progress = true;
        }
    );
    return progress;
}

static BlockChannelResponse BlockServer__execute(BlockServerPtr self, BlockServerConnectionPtr connection,
                                                 BlockChannelRequestPtr request)
{
    uint64_t client = ChannelServer__transport_pid(&connection->channel);
    if (request->operation == BLOCK_PROTOCOL_CLOSE)
    {
        return BlockChannelResponse__new(BufferClient__release_pair(&connection->buffers, client) == BUFFER_OK
                                             ? BLOCK_PROTOCOL_OK
                                             : BLOCK_PROTOCOL_IO_ERROR,
                                         0);
    }
    if (request->operation == BLOCK_PROTOCOL_DESCRIBE)
    {
        if (connection->buffers.read.id == 0)
        {
            match_result(
                BufferClient__acquire(&self->name, client, BUFFER_READ),
                success(buffer)
                {
                    connection->buffers.read = buffer;
                },
                error(code, message)
                {
                    return BlockChannelResponse__new(BLOCK_PROTOCOL_IO_ERROR, 0);
                }
            );
        }
        if (connection->buffers.write.id == 0)
        {
            match_result(
                BufferClient__acquire(&self->name, client, BUFFER_WRITE),
                success(buffer)
                {
                    connection->buffers.write = buffer;
                },
                error(code, message)
                {
                    return BlockChannelResponse__new(BLOCK_PROTOCOL_IO_ERROR, 0);
                }
            );
        }

        BlockChannelResponse response = BlockChannelResponse__new(BLOCK_PROTOCOL_OK, self->block_count);
        response.buffers = connection->buffers;
        return response;
    }
    BufferDescriptor descriptor =
        request->operation == BLOCK_PROTOCOL_READ ? connection->buffers.read : connection->buffers.write;
    if ((request->operation != BLOCK_PROTOCOL_READ && request->operation != BLOCK_PROTOCOL_WRITE) ||
        request->block >= self->block_count || request->count == 0 ||
        request->count > self->block_count - request->block ||
        request->count > BUFFER_SIZE / BLOCK_CHANNEL_SECTOR_SIZE || descriptor.id == 0 ||
        request->buffer != descriptor.id)
    {
        return BlockChannelResponse__new(BLOCK_PROTOCOL_INVALID_ARGUMENT, 0);
    }
    if (!BufferClient__validate(&descriptor, client, descriptor.direction))
    {
        return BlockChannelResponse__new(BLOCK_PROTOCOL_IO_ERROR, 0);
    }
    BlockProtocolStatus status =
        self->transfer(self->context, request->operation, request->block, request->count, &descriptor);
    return BlockChannelResponse__new(status, status == BLOCK_PROTOCOL_OK ? request->count : 0);
}

static bool BlockServer__serve(BlockServerPtr self, BlockServerConnectionPtr connection)
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
                        bool authorized = connection->authorized;
                        if (!authorized)
                        {
                            ServiceCode authorize_status = service_authorize_block_client(metadata.client_pid);
                            authorized = service_ok(authorize_status);
                        }
                        if (!authorized)
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
                            Optional(RequestTicket) previous =
                                ChannelRequestContext__swap(&Some(RequestTicket, connection->channel.ticket));
                            connection->response = BlockServer__execute(self, connection, &message);
                            ChannelRequestContext__swap(&previous);
                        }
                        connection->pending = true;
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
                    if (BufferClient__release_pair(&removed.buffers, ChannelServer__transport_pid(&removed.channel)) !=
                        BUFFER_OK)
                    {
                        index++;
                        continue;
                    }
                    ChannelServer__close(&removed.channel);
                    match_result(
                        BlockServerConnections__remove(&self->connections, removed),
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
                progress = BlockServer__serve(self, connection) || progress;
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
