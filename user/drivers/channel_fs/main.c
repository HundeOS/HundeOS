#define DRIVER_LOG_TAG "CHANNEL-FS"

#include <stdbool.h>
#include <stdint.h>

#include "channel/channel.h"
#include "sys/debug.h"
#include "sys/service.h"

#ifndef CHANNEL_FS_ENTRYPOINT
#define CHANNEL_FS_ENTRYPOINT main
#endif

#define CHANNEL_FS_BLOCK_SIZE 512ULL
#define CHANNEL_FS_READY_NAME "channel_fs"

typedef struct ChannelFs
{
    ServiceBlockChannelOpenRequest open_channel;
} ChannelFs;

typedef ChannelFs * ChannelFsPtr;

static ChannelFs ChannelFs__new(void)
{
    return (ChannelFs){
        .open_channel = ServiceBlockChannelOpenRequest__new(&bstr("")),
    };
}

static void ChannelFs__fill_pattern(uint8_t * data)
{
    for (size_t i = 0; i < CHANNEL_FS_BLOCK_SIZE; i++)
    {
        data[i] = (uint8_t)(i & 0xff);
    }
}

static bool ChannelFs__pattern_matches(uint8_t * data)
{
    for (size_t i = 0; i < CHANNEL_FS_BLOCK_SIZE; i++)
    {
        if (data[i] != (uint8_t)(i & 0xff))
        {
            return false;
        }
    }

    return true;
}

static BlockChannelStatus ChannelFs__submit(ChannelFsPtr self, BlockChannelMemoryPtr memory,
                                            BlockChannelOperation operation)
{
    uint64_t request_id = BlockChannelState__next_request_id(&memory->state);
    BlockChannelRequest request = BlockChannelRequest__new(request_id, operation, 0, 1, 0, CHANNEL_FS_BLOCK_SIZE);
    Channel channel = Channel__new(&memory->state.channel);
    uint64_t observed_epoch = Channel__data_epoch(&channel);
    BlockChannelStatus status = BlockChannelRequestRing__push(&memory->requests, &memory->state, &request);
    if (status != BLOCK_CHANNEL_OK)
    {
        if (BlockChannelState__producer_closed(&memory->state))
        {
            BlockChannelState__ack_producer_close(&memory->state);
        }
        return status;
    }
    Channel__notify_data(&channel);

    BlockChannelResponse response;
    while (true)
    {
        status = BlockChannelResponseRing__pop(&memory->responses, &memory->state, &response);
        if (status == BLOCK_CHANNEL_OK && response.request_id == request_id)
        {
            return response.status;
        }
        if (status == BLOCK_CHANNEL_CLOSED)
        {
            if (BlockChannelState__producer_closed(&memory->state))
            {
                BlockChannelState__ack_producer_close(&memory->state);
            }
            return status;
        }
        Channel__wait_for_data_change(&channel, observed_epoch);
        observed_epoch = Channel__data_epoch(&channel);
    }
}

static bool ChannelFs__exercise_channel(ChannelFsPtr self, BlockChannelMemoryPtr memory)
{
    ChannelFs__fill_pattern(memory->data);
    if (ChannelFs__submit(self, memory, BLOCK_CHANNEL_WRITE) != BLOCK_CHANNEL_OK)
    {
        return false;
    }

    for (size_t i = 0; i < CHANNEL_FS_BLOCK_SIZE; i++)
    {
        memory->data[i] = 0;
    }

    if (ChannelFs__submit(self, memory, BLOCK_CHANNEL_READ) != BLOCK_CHANNEL_OK)
    {
        return false;
    }

    return ChannelFs__pattern_matches(memory->data);
}

int CHANNEL_FS_ENTRYPOINT(int argc, char ** argv)
{
    if (argc != 2)
    {
        DRIVER_DEBUG("channel_fs: usage <device>\n");
        return 1;
    }

    BString device_path = BString__new(argv[1]);
    ChannelFs fs = ChannelFs__new();
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    if (!service_ok(service_block_open_channel(&device_path, &fs.open_channel)))
    {
        DRIVER_DEBUG("channel_fs: block channel open failed\n");
        return 1;
    }

    BlockChannelMemoryPtr memory = (BlockChannelMemoryPtr)fs.open_channel.channel;
    if (!ChannelFs__exercise_channel(&fs, memory))
    {
        DRIVER_DEBUG("channel_fs: channel transfer failed\n");
        return 1;
    }

    if (!service_ok(service_publish_ready(&bstr(CHANNEL_FS_READY_NAME))))
    {
        return 1;
    }

    service_block_close_channel(fs.open_channel.channel_id);
    return 0;
}
