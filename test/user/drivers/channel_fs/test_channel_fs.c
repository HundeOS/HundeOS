#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/service.h"
#include "../../../../common_includes/syscalls/syslog.h"

#define CHANNEL_FS_ENTRYPOINT channel_fs_main_under_test
#include "../../../../user/lib/channel/channel.c"
#include "../../../../user/drivers/channel_fs/main.c"

void setUp(void)
{
}

void tearDown(void)
{
}

SyslogResult kernel_syslogf(const char * format, ...)
{
    return SYSLOG_OK;
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
}

ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}

ServiceCode service_block_open_channel(ConstBStringPtr path, ServiceBlockChannelOpenRequestPtr channel)
{
    return SERVICE_OK;
}

ServiceCode service_block_close_channel(uint64_t channel_id)
{
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    return SERVICE_OK;
}

void test_channel_fs_submit_writes_request_and_consumes_matching_response(void)
{
    ChannelFs fs = ChannelFs__new();
    BlockChannelMemory memory;
    BlockChannelMemory__init(&memory);
    BlockChannelResponse response = BlockChannelResponse__new(1, BLOCK_CHANNEL_OK, 1, 0, CHANNEL_FS_BLOCK_SIZE);
    TEST_ASSERT_EQUAL(BLOCK_CHANNEL_OK, BlockChannelResponseRing__push(&memory.responses, &memory.state, &response));

    TEST_ASSERT_EQUAL(BLOCK_CHANNEL_OK, ChannelFs__submit(&fs, &memory, BLOCK_CHANNEL_READ));

    BlockChannelRequest request = BlockChannelRequest__new(0, BLOCK_CHANNEL_READ, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL(BLOCK_CHANNEL_OK, BlockChannelRequestRing__pop(&memory.requests, &memory.state, &request));
    TEST_ASSERT_EQUAL_UINT64(1, request.request_id);
    TEST_ASSERT_EQUAL(BLOCK_CHANNEL_READ, request.operation);
    TEST_ASSERT_EQUAL_UINT64(1, request.block_count);
    TEST_ASSERT_EQUAL_UINT64(CHANNEL_FS_BLOCK_SIZE, request.byte_count);
    TEST_ASSERT_EQUAL(BLOCK_CHANNEL_EMPTY, BlockChannelResponseRing__pop(&memory.responses, &memory.state, &response));
}
