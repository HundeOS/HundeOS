#include "unity.h"
#include "channel/block_client.h"
#include "channel/block_server.h"
#include "../services/channel_fixture.c"

static ServiceDirectoryPtr fixture_directory(void)
{
    return &directory;
}

#define ServiceDirectory__shared fixture_directory
#include "../../../user/lib/channel/block_client.c"
#include "../../../user/lib/channel/block_server.c"
#undef ServiceDirectory__shared

static BlockServer block_server;
static uint8_t disk[16][BLOCK_CHANNEL_SECTOR_SIZE];
static unsigned transfers;
static unsigned authorizations;
static bool authorized;
static bool fail_io;

ServiceCode service_authorize_block_client(uint64_t client_pid)
{
    TEST_ASSERT_EQUAL_UINT64(42, client_pid);
    authorizations++;
    return authorized ? SERVICE_OK : SERVICE_PERMISSION_DENIED;
}

static BlockProtocolStatus transfer(BlockServerContext context, BlockProtocolOperation operation,
                                    BlockProtocolBlockNumber block, BlockProtocolBlockCount count,
                                    ConstBufferDescriptorPtr buffer)
{
    ChannelDataPtr data = (ChannelDataPtr)buffer->address;
    transfers++;
    TEST_ASSERT_LESS_THAN_UINT64(16, block);
    if (fail_io)
    {
        return BLOCK_PROTOCOL_IO_ERROR;
    }
    if (operation == BLOCK_PROTOCOL_READ)
    {
        memcpy(data, disk[block], count * BLOCK_CHANNEL_SECTOR_SIZE);
    }
    else
    {
        memcpy(disk[block], data, count * BLOCK_CHANNEL_SECTOR_SIZE);
    }
    return BLOCK_PROTOCOL_OK;
}

static void serve(void)
{
    BlockServer__poll(&block_server);
}

void setUp(void)
{
    ChannelFixture__reset();
    directory.entries[1].name = bstr("/dev/virtio4");
    fixture_listener_available = true;
    current_pid = 8;
    block_server = unwrap_result(BlockServer__listen(&directory.entries[1].name, 16, disk, transfer), (BlockServer){0});
    fixture_server_step = serve;
    current_pid = 42;
    memset(disk, 0, sizeof(disk));
    transfers = 0;
    authorizations = 0;
    authorized = true;
    fail_io = false;
}

void tearDown(void)
{
}

static BlockClient open_client(void)
{
    match_result(BlockClient__open(&bstr("/dev/virtio4")),
        success(client)
        {
            return client;
        },
        error_test_fail()
    );
    return (BlockClient){0};
}

void test_block_client_reuses_channel_for_describe_write_and_multisector_read(void)
{
    BlockClient client = open_client();
    uint8_t expected[4096];
    uint8_t actual[4096];
    for (size_t index = 0; index < sizeof(expected); index++)
    {
        expected[index] = (uint8_t)(index * 7);
    }
    TEST_ASSERT_EQUAL_UINT64(16, client.block_count);
    TEST_ASSERT_EQUAL_UINT64(8,
                             unwrap_result(BlockClient__write(&client, 3, 8, (Address)expected, sizeof(expected)), 0));
    TEST_ASSERT_EQUAL_UINT64(8, unwrap_result(BlockClient__read(&client, 3, 8, (Address)actual, sizeof(actual)), 0));
    TEST_ASSERT_EQUAL_MEMORY(expected, actual, sizeof(expected));
    TEST_ASSERT_EQUAL_UINT64(1, allocated_pages);
    TEST_ASSERT_EQUAL_UINT(1, authorizations);
    TEST_ASSERT_EQUAL_UINT(2, transfers);
    TEST_ASSERT_EQUAL(CHANNEL_OK, BlockClient__close(&client));
    TEST_ASSERT_EQUAL(CHANNEL_OK, BlockClient__close(&client));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(1, released_pages);
    TEST_ASSERT_EQUAL_UINT64(0, BlockServerConnections__len(&block_server.connections));
}

void test_block_client_rejects_out_of_bounds_and_wrong_buffer_size(void)
{
    BlockClient client = open_client();
    uint8_t data[512];
    TEST_ASSERT_EQUAL_UINT64(0, unwrap_result(BlockClient__read(&client, 16, 1, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL_UINT64(0, unwrap_result(BlockClient__write(&client, 0, 2, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL_UINT(0, transfers);
}

void test_block_client_authorization_failure_closes_session(void)
{
    authorized = false;
    match_result(
        BlockClient__open(&bstr("/dev/virtio4")),
        success(client)
        {
            TEST_FAIL_MESSAGE("Unauthorized client opened block session");
        },
        error(code, message)
        {
            TEST_ASSERT_EQUAL(CHANNEL_INVALID_REPLY, code);
        }
    );
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(allocated_pages, released_pages);
    TEST_ASSERT_EQUAL_UINT(0, transfers);
}

void test_block_client_io_failure_does_not_copy_reply_data(void)
{
    BlockClient client = open_client();
    uint8_t data[512];
    memset(data, 0xa5, sizeof(data));
    fail_io = true;
    match_result(
        BlockClient__read(&client, 0, 1, (Address)data, sizeof(data)),
        success(count)
        {
            TEST_FAIL_MESSAGE("Expected IO error");
        },
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_IO_ERROR, code);
        }
    );
    TEST_ASSERT_EACH_EQUAL_UINT8(0xa5, data, sizeof(data));
}

void test_block_client_exit_without_close_releases_session(void)
{
    BlockClient client = open_client();
    QueueConnection__close(Queue__connection(&client.channel.control));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(allocated_pages, released_pages);
    TEST_ASSERT_EQUAL_UINT64(0, BlockServerConnections__len(&block_server.connections));
}

void test_block_server_rejects_stale_lease_and_buffer_overflow_before_hardware_access(void)
{
    BlockClient client = open_client();
    TEST_ASSERT_EQUAL_UINT64(8, Channel__server_pid(&client.channel));
    TEST_ASSERT_NOT_EQUAL(Channel__server_pid(&client.channel), Queue__peer_pid(&client.channel.control));
    BlockChannelRequest request = BlockChannelRequest__new(BLOCK_PROTOCOL_READ, 0);
    request.buffer = client.buffers.read.id + BUFFER_LIMIT;
    request.count = 1;
    BlockChannelResponse response;
    match_result(Channel__submit(&client.channel, (ConstChannelDataPtr)&request, sizeof(request),
                                 (ChannelDataPtr)&response, sizeof(response)),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_INVALID_ARGUMENT, response.status);
    request.buffer = client.buffers.read.id;
    request.count = BUFFER_SIZE / BLOCK_CHANNEL_SECTOR_SIZE + 1;
    match_result(Channel__submit(&client.channel, (ConstChannelDataPtr)&request, sizeof(request),
                                 (ChannelDataPtr)&response, sizeof(response)),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_INVALID_ARGUMENT, response.status);
    TEST_ASSERT_EQUAL_UINT(0, transfers);
}
