#include "unity.h"
#define main channel_fs_main_under_test
#include "../../../../user/drivers/channel_fs/main.c"
#undef main

static uint8_t storage[512];
static bool closed;
static bool published;
static bool fail_read;

void setUp(void)
{
    closed = false;
    published = false;
    fail_read = false;
}

void tearDown(void)
{
}

ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    published = true;
    return SERVICE_OK;
}

Result(BlockClient) BlockClient__open(ConstBStringPtr path)
{
    return Success(BlockClient, ((BlockClient){0}));
}

ChannelCode BlockClient__close(BlockClientPtr self)
{
    closed = true;
    return CHANNEL_OK;
}

Result(BlockProtocolBlockCount) BlockClient__write(BlockClientPtr self, BlockProtocolBlockNumber block,
                                                   BlockProtocolBlockCount count, Address buffer, size_t size)
{
    __builtin_memcpy(storage, (void *)buffer, sizeof(storage));
    return Success(BlockProtocolBlockCount, 1);
}

Result(BlockProtocolBlockCount) BlockClient__read(BlockClientPtr self, BlockProtocolBlockNumber block,
                                                  BlockProtocolBlockCount count, Address buffer, size_t size)
{
    if (fail_read)
    {
        return Error(BlockProtocolBlockCount, bstr("read failed"), BLOCK_PROTOCOL_IO_ERROR);
    }
    __builtin_memcpy((void *)buffer, storage, sizeof(storage));
    return Success(BlockProtocolBlockCount, 1);
}

void test_demo_uses_block_client_and_closes_session(void)
{
    char * argv[] = {"channel_fs", "/dev/test"};
    TEST_ASSERT_EQUAL(0, channel_fs_main_under_test(2, argv));
    TEST_ASSERT_TRUE(closed);
    TEST_ASSERT_TRUE(published);
}

void test_demo_closes_session_after_transfer_failure(void)
{
    fail_read = true;
    char * argv[] = {"channel_fs", "/dev/test"};
    TEST_ASSERT_EQUAL(1, channel_fs_main_under_test(2, argv));
    TEST_ASSERT_TRUE(closed);
    TEST_ASSERT_FALSE(published);
}
