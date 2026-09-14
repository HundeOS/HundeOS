#define DRIVER_LOG_TAG "CHANNEL-BLK"

#include <stdbool.h>
#include <stdint.h>

#include "channel/signal.h"
#include "sys/debug.h"
#include "sys/service.h"
#include "types/fixed_array.h"

#ifndef CHANNEL_BLK_ENTRYPOINT
#define CHANNEL_BLK_ENTRYPOINT main
#endif

#define CHANNEL_BLK_BLOCK_SIZE 512ULL
#define CHANNEL_BLK_BLOCK_COUNT 8ULL
#define CHANNEL_BLK_STORAGE_SIZE (CHANNEL_BLK_BLOCK_SIZE * CHANNEL_BLK_BLOCK_COUNT)

typedef struct ChannelBlkConnection
{
    BlockConnectionID connection_id;
    BlockProtocolMemoryPtr memory;
} ChannelBlkConnection;

typedef ChannelBlkConnection * ChannelBlkConnectionPtr;

OptionalDef(ChannelBlkConnection);

static bool ChannelBlkConnection__equals(ChannelBlkConnectionPtr self, ChannelBlkConnectionPtr other)
{
    return self->connection_id == other->connection_id;
}

FixedArrayDefine(ChannelBlkConnection, BLOCK_PROTOCOL_REQUEST_CAPACITY, ChannelBlkConnections,
                 (EqualFunction)ChannelBlkConnection__equals);
FixedArrayBodyDefine(ChannelBlkConnections);

typedef struct ChannelBlk
{
    BString path;
    uint8_t storage[CHANNEL_BLK_STORAGE_SIZE];
    ChannelBlkConnections connections;
} ChannelBlk;

typedef ChannelBlk * ChannelBlkPtr;

static ChannelBlk ChannelBlk__new(ConstBStringPtr path)
{
    return (ChannelBlk){
        .path = *path,
        .connections = ChannelBlkConnections__new(),
    };
}

static ChannelBlkConnection ChannelBlkConnection__new(BlockConnectionID connection_id, BlockProtocolMemoryPtr memory)
{
    return (ChannelBlkConnection){
        .connection_id = connection_id,
        .memory = memory,
    };
}

static BlockProtocolResponse ChannelBlk__handle_request(ChannelBlkPtr self, BlockProtocolRequestPtr request,
                                                        BlockProtocolMemoryPtr memory)
{
    size_t byte_count = request->block_count * CHANNEL_BLK_BLOCK_SIZE;
    size_t storage_offset = request->block * CHANNEL_BLK_BLOCK_SIZE;
    BlockProtocolStatus status = BLOCK_PROTOCOL_OK;
    uint64_t completed_blocks = request->block_count;

    if (request->block + request->block_count > CHANNEL_BLK_BLOCK_COUNT ||
        request->data_offset + byte_count > BLOCK_PROTOCOL_DATA_SIZE || request->byte_count < byte_count)
    {
        status = BLOCK_PROTOCOL_INVALID_ARGUMENT;
        completed_blocks = 0;
    }
    else if (request->operation == BLOCK_PROTOCOL_READ)
    {
        BlockProtocolMemory__write_data(memory, request->data_offset, self->storage + storage_offset, byte_count);
    }
    else if (request->operation == BLOCK_PROTOCOL_WRITE)
    {
        BlockProtocolMemory__read_data(memory, request->data_offset, self->storage + storage_offset, byte_count);
    }
    else
    {
        status = BLOCK_PROTOCOL_INVALID_ARGUMENT;
        completed_blocks = 0;
    }

    return BlockProtocolResponse__new(request->request_id, status, completed_blocks, request->data_offset, byte_count);
}

static void ChannelBlk__process_protocol(ChannelBlkPtr self, BlockProtocolMemoryPtr memory)
{
    BlockProtocolRequest request;
    while (BlockProtocolMemory__pop_request(memory, &request) == BLOCK_PROTOCOL_OK)
    {
        BlockProtocolResponse response = ChannelBlk__handle_request(self, &request, memory);
        response.data_offset = request.data_offset;
        BlockProtocolMemory__push_response(memory, &response);
        ChannelSignal signal = ChannelSignal__new(BlockProtocolMemory__channel_state(memory));
        ChannelSignal__notify_data(&signal);
    }
}

static void ChannelBlk__run_connection(ChannelBlkPtr self, BlockProtocolMemoryPtr memory)
{
    ChannelSignal signal = ChannelSignal__new(BlockProtocolMemory__channel_state(memory));
    while (!BlockProtocolMemory__closed(memory))
    {
        uint64_t observed_epoch = ChannelSignal__data_epoch(&signal);
        ChannelBlk__process_protocol(self, memory);
        if (BlockProtocolMemory__requests_empty(memory) && !BlockProtocolMemory__closed(memory))
        {
            ChannelSignal__wait_for_data_change(&signal, observed_epoch);
        }
    }
    if (BlockProtocolMemory__consumer_closed(memory))
    {
        BlockProtocolMemory__ack_consumer_close(memory);
    }
}

static void ChannelBlk__open_connection(ChannelBlkPtr self, ServiceEventPtr event)
{
    BlockProtocolMemoryPtr memory = (BlockProtocolMemoryPtr)event->data;
    ChannelBlkConnection connection = ChannelBlkConnection__new(event->source, memory);
    ChannelBlkConnections__push(&self->connections, connection);
    ChannelBlk__run_connection(self, memory);
}

static bool ChannelBlkConnection__id_matches(ChannelBlkConnectionPtr self, void * context)
{
    BlockConnectionID connection_id = *(BlockConnectionID *)context;
    return self->connection_id == connection_id;
}

static void ChannelBlk__close_connection(ChannelBlkPtr self, ServiceEventPtr event)
{
    BlockConnectionID connection_id = event->source;
    match_find_result(
        ChannelBlkConnections__find(&self->connections, ChannelBlkConnection__id_matches, &connection_id),
        some(connection) { ChannelBlkConnections__remove(&self->connections, connection); },
        none_do_nothing()
    );
}

static void ChannelBlk__handle_event(ChannelBlkPtr self, ServiceEventPtr event)
{
    if (event->type == SERVICE_EVENT_BLOCK_CONNECTION_OPENED)
    {
        ChannelBlk__open_connection(self, event);
        return;
    }
    if (event->type == SERVICE_EVENT_BLOCK_CONNECTION_CLOSED)
    {
        ChannelBlk__close_connection(self, event);
        return;
    }

    for_each_fa(ChannelBlkConnections, self->connections, index, connection,
                { ChannelBlk__process_protocol(self, connection.memory); });
}

int CHANNEL_BLK_ENTRYPOINT(int argc, char ** argv)
{
    if (argc != 2)
    {
        DRIVER_DEBUG("channel_blk: usage <device>\n");
        return 1;
    }

    BString path = BString__new(argv[1]);
    ChannelBlk driver = ChannelBlk__new(&path);
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    if (!service_ok(service_register_block_device(&driver.path, CHANNEL_BLK_BLOCK_SIZE, CHANNEL_BLK_BLOCK_COUNT)))
    {
        return 1;
    }
    if (!service_ok(service_publish_ready(&driver.path)))
    {
        return 1;
    }

    ServiceEvent event;
    for (ServiceCode status = service_wait(&event); service_alive(status); status = service_wait(&event))
    {
        if (service_ok(status))
        {
            ChannelBlk__handle_event(&driver, &event);
        }
    }

    return 0;
}
