#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/service.h"
#include "../../../../common_includes/syscalls/syslog.h"

#define CHANNEL_BLK_ENTRYPOINT channel_blk_main_under_test
#include "../../../../user/lib/channel/signal.c"
#include "../../../../user/drivers/channel_blk/main.c"

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

ServiceCode service_register_block_device(ConstBStringPtr path, uint64_t block_size, uint64_t block_count)
{
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    return SERVICE_OK;
}

ServiceCode service_wait(ServiceEventPtr event)
{
    return SERVICE_COMPLETE;
}

void test_channel_blk_processes_write_then_read_from_shared_channel(void)
{
    ChannelBlk driver = ChannelBlk__new(&bstr("/dev/channel-blk"));
    BlockProtocolMemory memory;
    BlockProtocolMemory__init(&memory);

    for (size_t i = 0; i < CHANNEL_BLK_BLOCK_SIZE; i++)
    {
        uint8_t value = (uint8_t)(255U - (i & 0xff));
        BlockProtocolMemory__write_data(&memory, i, &value, sizeof(value));
    }

    BlockProtocolRequest write_request =
        BlockProtocolRequest__new(1, BLOCK_PROTOCOL_WRITE, 0, 1, 0, CHANNEL_BLK_BLOCK_SIZE);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockProtocolMemory__push_request(&memory, &write_request));
    ChannelBlk__process_protocol(&driver, &memory);

    for (size_t i = 0; i < CHANNEL_BLK_BLOCK_SIZE; i++)
    {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(255U - (i & 0xff)), driver.storage[i]);
        uint8_t value = 0;
        BlockProtocolMemory__write_data(&memory, i, &value, sizeof(value));
    }

    BlockProtocolRequest read_request =
        BlockProtocolRequest__new(2, BLOCK_PROTOCOL_READ, 0, 1, 0, CHANNEL_BLK_BLOCK_SIZE);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockProtocolMemory__push_request(&memory, &read_request));
    ChannelBlk__process_protocol(&driver, &memory);

    for (size_t i = 0; i < CHANNEL_BLK_BLOCK_SIZE; i++)
    {
        uint8_t value = 0;
        BlockProtocolMemory__read_data(&memory, i, &value, sizeof(value));
        TEST_ASSERT_EQUAL_UINT8(driver.storage[i], value);
    }

    BlockProtocolResponse response = BlockProtocolResponse__new(0, BLOCK_PROTOCOL_EMPTY, 0, 0, 0);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockProtocolMemory__pop_response(&memory, &response));
    TEST_ASSERT_EQUAL_UINT64(1, response.request_id);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, response.status);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockProtocolMemory__pop_response(&memory, &response));
    TEST_ASSERT_EQUAL_UINT64(2, response.request_id);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, response.status);
}
