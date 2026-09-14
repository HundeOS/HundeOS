#include "unity.h"
#include "io/server.h"
#include "io/client.h"
#include "../services/channel_fixture.c"

static ServiceDirectoryPtr io_test_directory(void)
{
    return &directory;
}
#define ServiceDirectory__shared io_test_directory
#include "../../../user/lib/io/server.c"
#include "../../../user/lib/io/client.c"
#undef ServiceDirectory__shared

static IOService service;
static unsigned calls;
static unsigned disconnects;
static IORequest last_request;
static bool block_request;
static bool file_backend;
static uint64_t file_remaining;

static Optional(IOReply)
    execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    TEST_ASSERT_EQUAL_UINT64(42, pid);
    if (block_request)
    {
        return None(IOReply);
    }
    calls++;
    last_request = *request;
    IOReply reply = IOReply__new(IO_OK, 0);
    switch (request->operation)
    {
    case IO_REQUEST_OPEN:
    case IO_REQUEST_ACCEPT:
        reply.value = (file_backend ? IO_FILESYSTEM_HANDLE_BASE : IO_SESSION_HANDLE_BASE) + calls;
        break;
    case IO_REQUEST_READ:
        if (file_backend)
        {
            reply.value = file_remaining < request->count ? file_remaining : request->count;
            file_remaining -= reply.value;
            __builtin_memset(reply.data, 'f', reply.value);
            break;
        }
        reply.value = 2;
        reply.data[0] = 'o';
        reply.data[1] = 'k';
        break;
    case IO_REQUEST_WRITE:
        reply.value = request->count;
        break;
    case IO_REQUEST_CLOSE:
        break;
    default:
        reply.status = IO_OPERATION_NOT_SUPPORTED;
        break;
    }
    return Some(IOReply, reply);
}

static bool disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    disconnects++;
    return true;
}

static void serve(void)
{
    IOService__poll(&service);
}

void setUp(void)
{
    ChannelFixture__reset();
    directory.entries[1].name = bstr(IO_SYSTEM_SERVICE);
    fixture_listener_available = true;
    current_pid = 8;
    service = unwrap_result(IOService__listen(&bstr(IO_SYSTEM_SERVICE), 0, execute, disconnect), (IOService){0});
    fixture_server_step = serve;
    current_pid = 42;
    connected = false;
    failed = false;
    calls = 0;
    disconnects = 0;
    block_request = false;
    file_backend = false;
    file_remaining = 0;
}
void tearDown(void)
{
}

void test_file_client_combines_chunks_over_one_channel_and_stops_at_eof(void)
{
    file_backend = true;
    file_remaining = 600;
    IOHandle handle = IOClient__open(&bstr("/mnt/ext/file"), IO_OPEN_READ | IO_OPEN_WRITE);
    uint8_t bytes[700] = {0};
    TEST_ASSERT_EQUAL_INT64(700, IOClient__write(handle, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_UINT64(188, last_request.count);
    TEST_ASSERT_EQUAL_INT64(600, IOClient__read(handle, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_CHAR('f', bytes[0]);
    TEST_ASSERT_EQUAL_CHAR('f', bytes[599]);
    TEST_ASSERT_EQUAL_INT64(0, IOClient__read(handle, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL(IO_OK, IOClient__close(handle));
    TEST_ASSERT_EQUAL_UINT64(1, allocated_pages);
    Channel__close(&io_channel);
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(allocated_pages, released_pages);
}

void test_io_client_reuses_one_channel_for_multiple_handles_and_closes(void)
{
    IOHandle first = IOClient__open(&bstr("socket://one"), IO_OPEN_LISTEN);
    IOHandle second = IOClient__accept(first);
    TEST_ASSERT_GREATER_THAN_INT64(first, second);
    uint8_t bytes[300] = {0};
    TEST_ASSERT_EQUAL_INT64(IO_PROTOCOL_DATA_SIZE, IOClient__write(second, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_UINT64(IO_PROTOCOL_DATA_SIZE, last_request.count);
    TEST_ASSERT_EQUAL_INT64(2, IOClient__read(second, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_MEMORY("ok", bytes, 2);
    TEST_ASSERT_EQUAL(IO_OK, IOClient__close(second));
    TEST_ASSERT_EQUAL(IO_OK, IOClient__close(first));
    TEST_ASSERT_EQUAL_UINT64(1, allocated_pages);
    TEST_ASSERT_EQUAL_UINT(6, calls);
    TEST_ASSERT_EQUAL_UINT(0, disconnects);
    Channel__close(&io_channel);
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT(1, disconnects);
    TEST_ASSERT_EQUAL_UINT64(allocated_pages, released_pages);
}

void test_service_disconnects_client_with_pending_operation_without_executing_it(void)
{
    IOClient__open(&bstr("socket://one"), IO_OPEN_LISTEN);
    block_request = true;
    IORequest request = IORequest__new(IO_REQUEST_READ, IO_SESSION_HANDLE_BASE);
    match_result(Channel__request(&io_channel, (ConstChannelDataPtr)&request, sizeof(request)),
        success_do_nothing(),
                 error_test_fail()
    );
    ChannelFixture__drain();
    QueueConnection__close(Queue__connection(&io_channel.control));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT(1, calls);
    TEST_ASSERT_EQUAL_UINT(1, disconnects);
}
