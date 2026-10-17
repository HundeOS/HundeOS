#include "unity.h"
#include <string.h>

#define main block_partition_main_under_test
#include "../../../../user/drivers/block_partition/main.c"
#undef main

#define TEST_BLOCK_COUNT 32U

static uint8_t parent_storage[TEST_BLOCK_COUNT * BLOCK_CHANNEL_SECTOR_SIZE];
static BString registered_path;
static BString published_path;
static BlockProtocolBlockCount registered_count;
static BlockServerTransfer registered_transfer;
static BlockServerContext registered_context;
static bool parent_closed;

static void write_u32(uint8_t * bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void set_partition(uint64_t index, uint8_t type, uint32_t first_block, uint32_t block_count)
{
    size_t offset = BLOCK_PARTITION_TABLE_OFFSET + index * BLOCK_PARTITION_ENTRY_SIZE;
    parent_storage[offset + 4U] = type;
    write_u32(parent_storage + offset + 8U, first_block);
    write_u32(parent_storage + offset + 12U, block_count);
    parent_storage[510] = 0x55;
    parent_storage[511] = 0xaa;
}

void setUp(void)
{
    memset(parent_storage, 0, sizeof(parent_storage));
    registered_path = empty_bstr();
    published_path = empty_bstr();
    registered_count = 0;
    registered_transfer = 0;
    registered_context = 0;
    parent_closed = false;
}

void tearDown(void)
{
}

ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}

ServiceCode service_register_block_device(ConstBStringPtr path, uint64_t block_size, uint64_t block_count)
{
    TEST_ASSERT_EQUAL_UINT64(BLOCK_CHANNEL_SECTOR_SIZE, block_size);
    registered_path = *path;
    registered_count = block_count;
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr path)
{
    published_path = *path;
    return SERVICE_OK;
}

Result(BlockClient) BlockClient__open(ConstBStringPtr path)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/mmc0"), *path);
    return Success(BlockClient, ((BlockClient){.block_count = TEST_BLOCK_COUNT, .open = true}));
}

ChannelCode BlockClient__close(BlockClientPtr self)
{
    parent_closed = true;
    return CHANNEL_OK;
}

Result(BlockProtocolBlockCount) BlockClient__read(BlockClientPtr self, BlockProtocolBlockNumber block,
                                                  BlockProtocolBlockCount count, Address buffer, size_t size)
{
    if (block >= TEST_BLOCK_COUNT || count > TEST_BLOCK_COUNT - block || size != count * BLOCK_CHANNEL_SECTOR_SIZE)
    {
        return Error(BlockProtocolBlockCount, bstr("bad read"), BLOCK_PROTOCOL_INVALID_ARGUMENT);
    }
    memcpy((void *)buffer, parent_storage + block * BLOCK_CHANNEL_SECTOR_SIZE, size);
    return Success(BlockProtocolBlockCount, count);
}

Result(BlockProtocolBlockCount) BlockClient__write(BlockClientPtr self, BlockProtocolBlockNumber block,
                                                   BlockProtocolBlockCount count, Address buffer, size_t size)
{
    if (block >= TEST_BLOCK_COUNT || count > TEST_BLOCK_COUNT - block || size != count * BLOCK_CHANNEL_SECTOR_SIZE)
    {
        return Error(BlockProtocolBlockCount, bstr("bad write"), BLOCK_PROTOCOL_INVALID_ARGUMENT);
    }
    memcpy(parent_storage + block * BLOCK_CHANNEL_SECTOR_SIZE, (void *)buffer, size);
    return Success(BlockProtocolBlockCount, count);
}

ChannelCode BlockServer__listen_into(BlockServerPtr self, ConstBStringPtr name, BlockProtocolBlockCount block_count,
                                     BlockServerContext context, BlockServerTransfer transfer)
{
    registered_context = context;
    registered_transfer = transfer;
    return CHANNEL_OK;
}

int BlockServer__run(BlockServerPtr self)
{
    return 0;
}

void test_block_partition_rejects_invalid_mbr_and_out_of_bounds_entry(void)
{
    TEST_ASSERT_FALSE(
        is_something(BlockPartition__entry(parent_storage, BLOCK_PARTITION_MBR_SIZE, 1, TEST_BLOCK_COUNT)));
    set_partition(0, 0x83, 30, 4);
    TEST_ASSERT_FALSE(
        is_something(BlockPartition__entry(parent_storage, BLOCK_PARTITION_MBR_SIZE, 1, TEST_BLOCK_COUNT)));
}

void test_block_partition_translates_reads_and_writes_to_parent_blocks(void)
{
    BlockPartition partition = {
        .parent = {.block_count = TEST_BLOCK_COUNT, .open = true},
        .first_block = 8,
        .block_count = 4,
    };
    uint8_t data[BLOCK_CHANNEL_SECTOR_SIZE];
    memset(data, 0xa5, sizeof(data));
    BufferDescriptor buffer = {.address = (Address)data, .capacity = sizeof(data)};

    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockPartition__transfer(&partition, BLOCK_PROTOCOL_WRITE, 2, 1, &buffer));
    memset(data, 0, sizeof(data));
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockPartition__transfer(&partition, BLOCK_PROTOCOL_READ, 2, 1, &buffer));
    TEST_ASSERT_EACH_EQUAL_UINT8(0xa5, data, sizeof(data));
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_INVALID_ARGUMENT,
                      BlockPartition__transfer(&partition, BLOCK_PROTOCOL_READ, 4, 1, &buffer));
}

void test_block_partition_main_publishes_selected_mbr_partition(void)
{
    set_partition(1, 0x83, 8, 12);
    char * argv[] = {"block_partition", "/dev/mmc0", "2"};

    TEST_ASSERT_EQUAL(0, block_partition_main_under_test(3, argv));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/mmc0p2"), registered_path);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/mmc0p2"), published_path);
    TEST_ASSERT_EQUAL_UINT64(12, registered_count);
    TEST_ASSERT_NOT_NULL(registered_transfer);
    TEST_ASSERT_NOT_NULL(registered_context);
    TEST_ASSERT_FALSE(parent_closed);
}
