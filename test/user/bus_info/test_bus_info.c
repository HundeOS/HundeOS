#include "unity.h"
#include "bus/info.h"
#include "../services/channel_fixture.c"

static ServiceDirectoryPtr BusTest__directory(void)
{
    return &directory;
}
#define ServiceDirectory__shared BusTest__directory
#include "../../../user/lib/io/server.c"
#undef ServiceDirectory__shared

static IOService service;
static BusPciDevices devices;
static unsigned executions;
static unsigned disconnects;
static bool hold_request;

static Optional(IOReply)
    BusTest__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    TEST_ASSERT_EQUAL(42, pid);
    if (hold_request)
    {
        return None(IOReply);
    }
    executions++;
    return BusInfo__execute(context, session, pid, request);
}
static bool BusTest__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    disconnects++;
    return BusInfo__disconnect(context, session, pid);
}
static void BusTest__serve(void)
{
    IOService__poll(&service);
}
static Channel BusTest__open(void)
{
    return unwrap_result(Channel__open(&directory, &bstr(BUS_INFO_SERVICE)), ((Channel){0}));
}
static void BusTest__close(ChannelPtr channel)
{
    TEST_ASSERT_EQUAL(CHANNEL_OK, Channel__close(channel));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL(allocated_pages, released_pages);
    TEST_ASSERT_EQUAL(0, IOServiceConnections__len(&service.connections));
}
void setUp(void)
{
    ChannelFixture__reset();
    directory.entries[1].name = bstr(BUS_INFO_SERVICE);
    fixture_listener_available = true;
    devices = BusPciDevices__new();
    executions = 0;
    disconnects = 0;
    hold_request = false;
    current_pid = 8;
    service = unwrap_result(IOService__listen(&bstr(BUS_INFO_SERVICE), &devices, BusTest__execute, BusTest__disconnect),
                            ((IOService){0}));
    fixture_server_step = BusTest__serve;
    current_pid = 42;
}
void tearDown(void)
{
}

void test_bus_queries_page_complete_inventory_over_one_channel(void)
{
    for (unsigned i = 0; i < BUS_INFO_MAX_DEVICES; i++)
    {
        BusPciDevice device = {.bus = i / 32, .device = i % 32, .vendor_id = 0x1234, .device_id = i};
        match_result(BusPciDevices__push(&devices, device),
            success_do_nothing(),
            error_test_fail()
        );
    }
    Channel channel = BusTest__open();
    for (unsigned i = 0; i <= BUS_INFO_MAX_DEVICES; i++)
    {
        waits = 0;
        match_result(
            BusInfo__read(&channel, i),
            success(page)
            {
                TEST_ASSERT_EQUAL(BUS_INFO_MAX_DEVICES, page.total);
                TEST_ASSERT_EQUAL(i < BUS_INFO_MAX_DEVICES ? 1 : 0, page.count);
                if (page.count == 1)
                {
                    TEST_ASSERT_EQUAL(i, page.device.device_id);
                    TEST_ASSERT_EQUAL(0x1234, page.device.vendor_id);
                }
            },
            error_test_fail()
        );
    }
    TEST_ASSERT_EQUAL(1, allocated_pages);
    BusTest__close(&channel);
    TEST_ASSERT_EQUAL(1, disconnects);
}

void test_empty_bus_and_invalid_offset(void)
{
    Channel channel = BusTest__open();
    match_result(
        BusInfo__read(&channel, 0),
        success(page)
        {
            TEST_ASSERT_EQUAL(0, page.total);
            TEST_ASSERT_EQUAL(0, page.count);
        },
        error_test_fail()
    );
    match_result(
        BusInfo__read(&channel, UINT64_MAX),
        success(val)
        {
            TEST_FAIL_MESSAGE("Invalid offset accepted");
        },
        error(code, message)
        {
            TEST_ASSERT_EQUAL(IO_INVALID_PATH, code);
        }
    );
    BusTest__close(&channel);
}

void test_bus_rejects_write_bad_handle_and_wrong_message_size(void)
{
    Channel channel = BusTest__open();
    IORequest request = BusInfo__request(0);
    IOReply reply;
    request.operation = IO_REQUEST_WRITE;
    match_result(Channel__submit(&channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&reply,
                                 sizeof(reply)),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, reply.status);
    request = BusInfo__request(0);
    request.handle = 7;
    match_result(Channel__submit(&channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&reply,
                                 sizeof(reply)),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, reply.status);
    match_result(Channel__submit(&channel, (ConstChannelDataPtr)&request, sizeof(request) - 1, (ChannelDataPtr)&reply,
                                 sizeof(reply)),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, reply.status);
    BusTest__close(&channel);
}

void test_client_departure_during_query_releases_channel(void)
{
    Channel channel = BusTest__open();
    hold_request = true;
    IORequest request = BusInfo__request(0);
    match_result(Channel__request(&channel, (ConstChannelDataPtr)&request, sizeof(request)),
        success_do_nothing(),
                 error_test_fail()
    );
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL(QUEUE_OK, QueueConnection__close(Queue__connection(&channel.control)));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL(0, executions);
    TEST_ASSERT_EQUAL(1, disconnects);
    TEST_ASSERT_EQUAL(allocated_pages, released_pages);
}

void test_bus_reply_backpressure_retains_reply_without_reexecuting_query(void)
{
    Channel channel = BusTest__open();
    hold_request = true;
    IORequest request = BusInfo__request(0);
    match_result(Channel__request(&channel, (ConstChannelDataPtr)&request, sizeof(request)),
        success_do_nothing(),
                 error_test_fail()
    );
    ChannelFixture__drain();
    match_optional(
        IOServiceConnections__ptr_at(&service.connections, 0),
        some(connection)
        {
            QueueMessage filler = ChannelControlMessage__new(99, 0, 0, 0);
            for (unsigned i = 0; i < QUEUE_CAPACITY; i++)
            {
                ChannelFixture__push(&connection->channel.control, &filler);
            }
            hold_request = false;
            current_pid = 8;
            IOService__poll(&service);
            IOService__poll(&service);
            TEST_ASSERT_EQUAL(IO_SERVICE_REPLY, connection->phase);
            TEST_ASSERT_EQUAL(1, executions);
        },
        none_test_fail()
    );
    match_optional(
        ChannelSessions__ptr_at(&exchanges, 0),
        some(exchange)
        {
            match_optional(
                exchange->server,
                some(server)
                {
                    for (unsigned i = 0; i < QUEUE_CAPACITY; i++)
                    {
                        match_result(Queue__pop(&server),
                            success_do_nothing(),
                            error_test_fail()
                        );
                    }
                    exchange->server = Some(Queue, server);
                },
                none_test_fail()
            );
        },
        none_test_fail()
    );
    current_pid = 42;
    ChannelFixture__drain();
    IOReply reply;
    match_result(
        Channel__poll_reply(&channel, (ChannelDataPtr)&reply, sizeof(reply)),
        success(size)
        {
            match_optional(size,
                some(bytes)
                {
                    TEST_ASSERT_EQUAL(sizeof(reply), bytes);
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(1, executions);
    BusTest__close(&channel);
}
