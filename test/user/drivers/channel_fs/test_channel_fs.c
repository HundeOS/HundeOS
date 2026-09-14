#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/service.h"
#include "../../../../common_includes/syscalls/syslog.h"

#define CHANNEL_FS_ENTRYPOINT channel_fs_main_under_test
#include "../../../../user/lib/channel/signal.c"
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

ServiceCode service_block_connect(ConstBStringPtr path, ServiceBlockConnectionRequestPtr channel)
{
    return SERVICE_OK;
}

ServiceCode service_block_disconnect(uint64_t channel_id)
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
    BlockProtocolMemory memory;
    BlockProtocolMemory__init(&memory);
    BlockProtocolResponse response = BlockProtocolResponse__new(1, BLOCK_PROTOCOL_OK, 1, 0, CHANNEL_FS_BLOCK_SIZE);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockProtocolMemory__push_response(&memory, &response));

    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, ChannelFs__submit(&fs, &memory, BLOCK_PROTOCOL_READ));

    BlockProtocolRequest request = BlockProtocolRequest__new(0, BLOCK_PROTOCOL_READ, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockProtocolMemory__pop_request(&memory, &request));
    TEST_ASSERT_EQUAL_UINT64(1, request.request_id);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_READ, request.operation);
    TEST_ASSERT_EQUAL_UINT64(1, request.block_count);
    TEST_ASSERT_EQUAL_UINT64(CHANNEL_FS_BLOCK_SIZE, request.byte_count);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_EMPTY, BlockProtocolMemory__pop_response(&memory, &response));
}
