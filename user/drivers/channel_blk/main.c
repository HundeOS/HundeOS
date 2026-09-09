#define DRIVER_LOG_TAG "CHANNEL-BLK"

#include <stdbool.h>
#include <stdint.h>

#include "channel/channel.h"
#include "sys/debug.h"
#include "sys/service.h"
#include "types/fixed_array.h"

#ifndef CHANNEL_BLK_ENTRYPOINT
#define CHANNEL_BLK_ENTRYPOINT main
#endif

#define CHANNEL_BLK_BLOCK_SIZE 512ULL
#define CHANNEL_BLK_BLOCK_COUNT 8ULL
#define CHANNEL_BLK_STORAGE_SIZE (CHANNEL_BLK_BLOCK_SIZE * CHANNEL_BLK_BLOCK_COUNT)

typedef struct ChannelBlkOpenChannel
{
    uint64_t channel_id;
    BlockChannelMemoryPtr memory;
} ChannelBlkOpenChannel;

typedef ChannelBlkOpenChannel * ChannelBlkOpenChannelPtr;

OptionalDef(ChannelBlkOpenChannel);

static bool ChannelBlkOpenChannel__equals(ChannelBlkOpenChannelPtr self, ChannelBlkOpenChannelPtr other)
{
    return self->channel_id == other->channel_id;
}

FixedArrayDefine(ChannelBlkOpenChannel, BLOCK_CHANNEL_REQUEST_CAPACITY, ChannelBlkOpenChannels,
                 (EqualFunction)ChannelBlkOpenChannel__equals);
FixedArrayBodyDefine(ChannelBlkOpenChannels);

typedef struct ChannelBlk
{
    BString path;
    uint8_t storage[CHANNEL_BLK_STORAGE_SIZE];
    ChannelBlkOpenChannels channels;
} ChannelBlk;

typedef ChannelBlk * ChannelBlkPtr;

static ChannelBlk ChannelBlk__new(ConstBStringPtr path)
{
    return (ChannelBlk){
        .path = *path,
        .channels = ChannelBlkOpenChannels__new(),
    };
}

static ChannelBlkOpenChannel ChannelBlkOpenChannel__new(uint64_t channel_id, BlockChannelMemoryPtr memory)
{
    return (ChannelBlkOpenChannel){
        .channel_id = channel_id,
        .memory = memory,
    };
}

static void ChannelBlk__copy(uint8_t * target, uint8_t * source, size_t byte_count)
{
    for (size_t i = 0; i < byte_count; i++)
    {
        target[i] = source[i];
    }
}

static BlockChannelResponse ChannelBlk__handle_request(ChannelBlkPtr self, BlockChannelRequestPtr request,
                                                       uint8_t * channel_data)
{
    size_t byte_count = request->block_count * CHANNEL_BLK_BLOCK_SIZE;
    size_t storage_offset = request->block * CHANNEL_BLK_BLOCK_SIZE;
    BlockChannelStatus status = BLOCK_CHANNEL_OK;
    uint64_t completed_blocks = request->block_count;

    if (request->block + request->block_count > CHANNEL_BLK_BLOCK_COUNT ||
        request->data_offset + byte_count > BLOCK_CHANNEL_DATA_SIZE || request->byte_count < byte_count)
    {
        status = BLOCK_CHANNEL_INVALID_ARGUMENT;
        completed_blocks = 0;
    }
    else if (request->operation == BLOCK_CHANNEL_READ)
    {
        ChannelBlk__copy(channel_data + request->data_offset, self->storage + storage_offset, byte_count);
    }
    else if (request->operation == BLOCK_CHANNEL_WRITE)
    {
        ChannelBlk__copy(self->storage + storage_offset, channel_data + request->data_offset, byte_count);
    }
    else
    {
        status = BLOCK_CHANNEL_INVALID_ARGUMENT;
        completed_blocks = 0;
    }

    return BlockChannelResponse__new(request->request_id, status, completed_blocks, request->data_offset, byte_count);
}

static void ChannelBlk__process_channel(ChannelBlkPtr self, BlockChannelMemoryPtr memory)
{
    BlockChannelRequest request;
    while (BlockChannelRequestRing__pop(&memory->requests, &memory->state, &request) == BLOCK_CHANNEL_OK)
    {
        BlockChannelResponse response = ChannelBlk__handle_request(self, &request, memory->data);
        response.data_offset = request.data_offset;
        BlockChannelResponseRing__push(&memory->responses, &memory->state, &response);
        Channel channel = Channel__new(&memory->state.channel);
        Channel__notify_data(&channel);
    }
}

static void ChannelBlk__run_channel(ChannelBlkPtr self, BlockChannelMemoryPtr memory)
{
    Channel channel = Channel__new(&memory->state.channel);
    while (!BlockChannelState__closed(&memory->state))
    {
        uint64_t observed_epoch = Channel__data_epoch(&channel);
        ChannelBlk__process_channel(self, memory);
        if (BlockChannelRequestRing__empty(&memory->requests) && !BlockChannelState__closed(&memory->state))
        {
            Channel__wait_for_data_change(&channel, observed_epoch);
        }
    }
    if (BlockChannelState__consumer_closed(&memory->state))
    {
        BlockChannelState__ack_consumer_close(&memory->state);
    }
}

static void ChannelBlk__open_channel(ChannelBlkPtr self, ServiceEventPtr event)
{
    BlockChannelMemoryPtr memory = (BlockChannelMemoryPtr)event->data;
    ChannelBlkOpenChannel channel = ChannelBlkOpenChannel__new(event->source, memory);
    ChannelBlkOpenChannels__push(&self->channels, channel);
    ChannelBlk__run_channel(self, memory);
}

static bool ChannelBlkOpenChannel__id_matches(ChannelBlkOpenChannelPtr self, void * context)
{
    uint64_t channel_id = *(uint64_t *)context;
    return self->channel_id == channel_id;
}

static void ChannelBlk__close_channel(ChannelBlkPtr self, ServiceEventPtr event)
{
    uint64_t channel_id = event->source;
    match_find_result(
        ChannelBlkOpenChannels__find(&self->channels, ChannelBlkOpenChannel__id_matches, &channel_id),
        some(channel) { ChannelBlkOpenChannels__remove(&self->channels, channel); },
        none_do_nothing()
    );
}

static void ChannelBlk__handle_event(ChannelBlkPtr self, ServiceEventPtr event)
{
    if (event->type == SERVICE_EVENT_BLOCK_CHANNEL_OPEN)
    {
        ChannelBlk__open_channel(self, event);
        return;
    }
    if (event->type == SERVICE_EVENT_BLOCK_CHANNEL_CLOSED)
    {
        ChannelBlk__close_channel(self, event);
        return;
    }

    for_each_fa(ChannelBlkOpenChannels, self->channels, index, channel,
                { ChannelBlk__process_channel(self, channel.memory); });
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
