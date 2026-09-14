#define DRIVER_LOG_TAG "CHANNEL-FS"

#include <stdbool.h>
#include <stdint.h>

#include "channel/signal.h"
#include "sys/debug.h"
#include "sys/service.h"

#ifndef CHANNEL_FS_ENTRYPOINT
#define CHANNEL_FS_ENTRYPOINT main
#endif

#define CHANNEL_FS_BLOCK_SIZE 512ULL
#define CHANNEL_FS_READY_NAME "channel_fs"

typedef struct ChannelFs
{
    ServiceBlockConnectionRequest connection;
} ChannelFs;

typedef ChannelFs * ChannelFsPtr;

static ChannelFs ChannelFs__new(void)
{
    return (ChannelFs){
        .connection = ServiceBlockConnectionRequest__new(&bstr("")),
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

static BlockProtocolStatus ChannelFs__submit(ChannelFsPtr self, BlockProtocolMemoryPtr memory,
                                             BlockProtocolOperation operation)
{
    BlockProtocolRequestID request_id = BlockProtocolMemory__next_request_id(memory);
    BlockProtocolRequest request = BlockProtocolRequest__new(request_id, operation, 0, 1, 0, CHANNEL_FS_BLOCK_SIZE);
    ChannelSignal channel = ChannelSignal__new(BlockProtocolMemory__channel_state(memory));
    uint64_t observed_epoch = ChannelSignal__data_epoch(&channel);
    BlockProtocolStatus status = BlockProtocolMemory__push_request(memory, &request);
    if (status != BLOCK_PROTOCOL_OK)
    {
        if (BlockProtocolMemory__producer_closed(memory))
        {
            BlockProtocolMemory__ack_producer_close(memory);
        }
        return status;
    }
    ChannelSignal__notify_data(&channel);

    BlockProtocolResponse response;
    while (true)
    {
        status = BlockProtocolMemory__pop_response(memory, &response);
        if (status == BLOCK_PROTOCOL_OK && response.request_id == request_id)
        {
            return response.status;
        }
        if (status == BLOCK_PROTOCOL_CLOSED)
        {
            if (BlockProtocolMemory__producer_closed(memory))
            {
                BlockProtocolMemory__ack_producer_close(memory);
            }
            return status;
        }
        ChannelSignal__wait_for_data_change(&channel, observed_epoch);
        observed_epoch = ChannelSignal__data_epoch(&channel);
    }
}

static bool ChannelFs__exercise_channel(ChannelFsPtr self, BlockProtocolMemoryPtr memory)
{
    uint8_t data[CHANNEL_FS_BLOCK_SIZE];
    ChannelFs__fill_pattern(data);
    if (BlockProtocolMemory__write_data(memory, 0, data, sizeof(data)) != BLOCK_PROTOCOL_OK)
    {
        return false;
    }
    if (ChannelFs__submit(self, memory, BLOCK_PROTOCOL_WRITE) != BLOCK_PROTOCOL_OK)
    {
        return false;
    }

    __builtin_memset(data, 0, sizeof(data));
    if (BlockProtocolMemory__write_data(memory, 0, data, sizeof(data)) != BLOCK_PROTOCOL_OK)
    {
        return false;
    }

    if (ChannelFs__submit(self, memory, BLOCK_PROTOCOL_READ) != BLOCK_PROTOCOL_OK)
    {
        return false;
    }

    if (BlockProtocolMemory__read_data(memory, 0, data, sizeof(data)) != BLOCK_PROTOCOL_OK)
    {
        return false;
    }
    return ChannelFs__pattern_matches(data);
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
    if (!service_ok(service_block_connect(&device_path, &fs.connection)))
    {
        DRIVER_DEBUG("channel_fs: block connection open failed\n");
        return 1;
    }

    BlockProtocolMemoryPtr memory = (BlockProtocolMemoryPtr)fs.connection.shared_memory;
    if (!ChannelFs__exercise_channel(&fs, memory))
    {
        DRIVER_DEBUG("channel_fs: channel transfer failed\n");
        return 1;
    }

    if (!service_ok(service_publish_ready(&bstr(CHANNEL_FS_READY_NAME))))
    {
        return 1;
    }

    service_block_disconnect(fs.connection.connection_id);
    return 0;
}
