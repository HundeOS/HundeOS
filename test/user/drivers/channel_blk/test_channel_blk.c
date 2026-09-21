#include "unity.h"
#define main channel_blk_main_under_test
#include "../../../../user/drivers/channel_blk/main.c"
#undef main

void setUp(void)
{
}
void tearDown(void)
{
}
ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}
ServiceCode service_register_block_device(ConstBStringPtr path, uint64_t size, uint64_t count)
{
    return SERVICE_OK;
}
ServiceCode service_publish_ready(ConstBStringPtr name)
{
    return SERVICE_OK;
}
Result(BlockServer) BlockServer__listen(ConstBStringPtr name, BlockProtocolBlockCount count, BlockServerContext context,
                                        BlockServerTransfer transfer)
{
    return Success(BlockServer, ((BlockServer){.context = context, .transfer = transfer}));
}

ChannelCode BlockServer__listen_into(BlockServerPtr self, ConstBStringPtr name, BlockProtocolBlockCount count,
                                     BlockServerContext context, BlockServerTransfer transfer)
{
    *self = (BlockServer){.context = context, .transfer = transfer};
    return CHANNEL_OK;
}

int BlockServer__run(BlockServerPtr self)
{
    return 0;
}

void test_channel_blk_reads_and_writes_shared_buffers(void)
{
    ChannelBlk device = {0};
    uint8_t input[1024];
    uint8_t output[1024] = {0};
    for (size_t index = 0; index < sizeof(input); index++)
    {
        input[index] = (uint8_t)index;
    }
    BufferDescriptor write_buffer = {.address = (Address)input, .capacity = sizeof(input)};
    BufferDescriptor read_buffer = {.address = (Address)output, .capacity = sizeof(output)};
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, ChannelBlk__transfer(&device, BLOCK_PROTOCOL_WRITE, 2, 2, &write_buffer));
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, ChannelBlk__transfer(&device, BLOCK_PROTOCOL_READ, 2, 2, &read_buffer));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, output, sizeof(input));
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_INVALID_ARGUMENT,
                      ChannelBlk__transfer(&device, BLOCK_PROTOCOL_READ, UINT64_MAX, 2, &read_buffer));
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_INVALID_ARGUMENT,
                      ChannelBlk__transfer(&device, BLOCK_PROTOCOL_READ, 0, 3, &read_buffer));
}
