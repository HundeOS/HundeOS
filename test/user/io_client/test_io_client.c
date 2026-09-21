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
            __builtin_memset(IORequest__read_target(request, &reply), 'f', reply.value);
            break;
        }
        reply.value = 2;
        IORequest__read_target(request, &reply)[0] = 'o';
        IORequest__read_target(request, &reply)[1] = 'k';
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
    open_handles = 0;
    buffers = BufferPair__new();
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
    TEST_ASSERT_EQUAL_UINT64(700, last_request.count);
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
    TEST_ASSERT_EQUAL_INT64(sizeof(bytes), IOClient__write(second, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_UINT64(sizeof(bytes), last_request.count);
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

void test_link_transports_both_paths_and_returns_provider_error(void)
{
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, IOClient__link(&bstr("/source"), &bstr("/target")));
    TEST_ASSERT_EQUAL(IO_REQUEST_LINK, last_request.operation);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/source"), IORequest__path(&last_request));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/target"), IORequest__target_path(&last_request));
    TEST_ASSERT_EQUAL_UINT(1, calls);
}

void test_kernel_channel_delegates_pid_but_cannot_change_it_mid_session(void)
{
    QueueIdentity identity = {.connection = 31, .client_pid = 7, .server_pid = 8, .origin_pid = 7, .accepted = 1};
    QueuePage warden_page = {0};
    QueuePage server_page = {0};
    Queue warden = Queue__new(&identity, &warden_page, &server_page, identity.connection, 8);
    Queue server_queue = Queue__new(&identity, &server_page, &warden_page, identity.connection, 7);
    ChannelServer server = {.control = server_queue};
    ChannelPayload payload = {0};
    QueueMessage request = ChannelControlMessage__new(CHANNEL_REQUEST_SERVE, (uint64_t)&payload, sizeof(IORequest), 99);
    request.arg3 = QUEUE_KERNEL_PID;
    request.request = (RequestMetadata){.generation = 1, .origin_pid = 42};

    ChannelFixture__push(&warden, &request);

    match_result(
        ChannelServer__poll(&server),
        success(incoming)
        {
            match_optional(
                incoming,
                some(metadata)
                {
                    TEST_ASSERT_EQUAL_UINT64(sizeof(IORequest), metadata.size);
                    TEST_ASSERT_EQUAL_UINT64(42, metadata.client_pid);
                    TEST_ASSERT_EQUAL_UINT64(42, ChannelServer__client_pid(&server));
                    TEST_ASSERT_EQUAL_UINT64(QUEUE_KERNEL_PID, ChannelServer__transport_pid(&server));
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_userspace_channel_cannot_impersonate_kernel_delegation(void)
{
    IORequest request = IORequest__new(IO_REQUEST_OPEN, 0);
    request.client_pid = QUEUE_KERNEL_PID;
    TEST_ASSERT_EQUAL(IO_OK, IOClient__submit(&request).status);
    TEST_ASSERT_EQUAL_UINT(1, calls);
}

void test_bulk_read_spans_buffer_capacity_and_last_close_releases_leases(void)
{
    file_backend = true;
    file_remaining = BUFFER_SIZE + 300;
    IOHandle handle = IOClient__open(&bstr("/large"), IO_OPEN_READ);
    BufferID first = buffers.read.id;
    uint8_t * bytes = malloc(BUFFER_SIZE + 400);
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT64(BUFFER_SIZE + 300, IOClient__read(handle, bytes, BUFFER_SIZE + 400));
    TEST_ASSERT_EQUAL_UINT(4, calls);
    TEST_ASSERT_EACH_EQUAL_UINT8('f', bytes, BUFFER_SIZE + 300);
    free(bytes);
    TEST_ASSERT_EQUAL(IO_OK, IOClient__close(handle));
    TEST_ASSERT_EQUAL_UINT64(0, buffers.read.id);
    IOClient__open(&bstr("/again"), IO_OPEN_READ);
    TEST_ASSERT_NOT_EQUAL(first, buffers.read.id);
}

void test_service_rejects_wrong_lease_and_oversized_transfer_before_execution(void)
{
    IOHandle handle = IOClient__open(&bstr("/file"), IO_OPEN_READ);
    IORequest request = IORequest__new(IO_REQUEST_READ, handle);
    request.buffered = true;
    request.buffer = buffers.read.id + BUFFER_LIMIT;
    request.count = 1;
    request.transfer_address = 7;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, IOClient__submit(&request).status);
    request.buffer = buffers.read.id;
    request.count = BUFFER_SIZE + 1;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, IOClient__submit(&request).status);
    TEST_ASSERT_EQUAL_UINT(1, calls);
}
