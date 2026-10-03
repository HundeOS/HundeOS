#include "unity.h"
#include "io/server.h"
#include "io/client.h"
#include "../services/channel_fixture.c"

static ServiceDirectoryPtr io_test_directory(void)
{
    return &directory;
}

#define ServiceDirectory__shared io_test_directory

void QueueWait__reset_after_fork(void)
{
}

#include "../../../user/lib/io/inheritance.c"
#include "../../../user/lib/io/server.c"
#include "../../../user/lib/io/client.c"
#undef ServiceDirectory__shared

#define TEST_BACKEND_SERVICE "test-io-backend"

static IOService service;
static unsigned route_calls;
static unsigned backend_calls;
static unsigned disconnects;
static IORequest last_request;
static bool block_request;
static bool file_backend;
static bool route_forever;
static bool route_without_service;
static uint64_t file_remaining;
static IOHandle next_backend_handle;

static bool path_operation(IORequestOperation operation)
{
    return operation == IO_REQUEST_OPEN || operation == IO_REQUEST_LIST_DIR || operation == IO_REQUEST_FILE_INFO ||
           operation == IO_REQUEST_MKDIR || operation == IO_REQUEST_REMOVE || operation == IO_REQUEST_LINK ||
           operation == IO_REQUEST_PIPE_CREATE;
}

static Optional(IOReply)
    execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    TEST_ASSERT_EQUAL_UINT64(42, pid);
    TEST_ASSERT_EQUAL_UINT64(42, request->client_pid);
    TEST_ASSERT_EQUAL_UINT64(session, request->client_session);

    if (path_operation(request->operation) && (!request->buffered || route_forever || route_without_service))
    {
        route_calls++;
        BString path = IORequest__path(request);
        BString target = IORequest__target_path(request);
        if (route_without_service)
        {
            return Some(IOReply, IOReply__redirect(&empty_bstr(), &path, &target, true));
        }
        return Some(IOReply, IOReply__redirect(&bstr(TEST_BACKEND_SERVICE), &path, &target, true));
    }
    if (block_request)
    {
        return None(IOReply);
    }

    backend_calls++;
    last_request = *request;
    IOReply reply = IOReply__new(IO_OK, 0);
    switch (request->operation)
    {
    case IO_REQUEST_OPEN:
    case IO_REQUEST_ACCEPT:
        reply.value = next_backend_handle++;
        break;
    case IO_REQUEST_PIPE_CREATE:
        reply.pipe = (IOPipe){.read = next_backend_handle++, .write = next_backend_handle++};
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
    case IO_REQUEST_SEEK:
        reply.value = request->offset;
        break;
    case IO_REQUEST_WATCH_FILE:
        reply.file_change = IOFileChange__new(request->handle, 7, IO_FILE_CHANGE_WRITE);
        break;
    case IO_REQUEST_GET_TERMINAL_ATTRIBUTES:
        reply.terminal_attributes = IOTerminalAttributes__default();
        break;
    case IO_REQUEST_WAIT_FILE_CHANGE:
        reply.file_change = IOFileChange__new(IORequest__file_watches(request)[0].handle, 8, IO_FILE_CHANGE_DELETE);
        break;
    case IO_REQUEST_CLOSE:
    case IO_REQUEST_SET_FOREGROUND_PROCESS:
    case IO_REQUEST_SET_TERMINAL_ATTRIBUTES:
    case IO_REQUEST_LINK:
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

static IOClientSessionPtr backend_session(void)
{
    return unwrap_optional(IOClient__find_session(&bstr(TEST_BACKEND_SERVICE)), NULL);
}

void test_io_client_reports_channel_wait_interruption_without_discarding_session(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL_INT64(IO_SESSION_HANDLE_BASE, IOClient__open(&bstr("/mnt/ext/file"), IO_OPEN_READ));
    IOClientSessionPtr session = backend_session();
    TEST_ASSERT_NOT_NULL(session);
    IORequest request = IORequest__new(IO_REQUEST_READ, IO_FILESYSTEM_HANDLE_BASE);
    fixture_wait_interrupted = true;

    TEST_ASSERT_EQUAL(IO_INTERRUPTED, IOClient__submit_to(session, &request).status);
    TEST_ASSERT_FALSE(session->failed);
}

static void close_client_channels(void)
{
    for (ArrayIndex index = 0; index < IOClientSessions__len(&sessions); index++)
    {
        match_optional(
            IOClientSessions__ptr_at(&sessions, index),
            some(session)
            {
                Channel__close(&session->channel);
                if (session->watch_connected)
                {
                    Channel__close(&session->watch_channel);
                }
            },
            none_do_nothing()
        );
    }
    ChannelFixture__drain();
}

void setUp(void)
{
    ChannelFixture__reset();
    directory.entries[1].name = bstr(IO_SYSTEM_SERVICE);
    directory.entries[2] = directory.entries[1];
    directory.entries[2].name = bstr(TEST_BACKEND_SERVICE);
    fixture_listener_available = true;
    current_pid = 8;
    service = unwrap_result(IOService__listen(&bstr(IO_SYSTEM_SERVICE), 0, execute, disconnect), (IOService){0});
    fixture_server_step = serve;
    current_pid = 42;
    sessions = IOClientSessions__new();
    handles = IOClientHandles__new();
    next_handle = IO_SESSION_HANDLE_BASE;
    route_calls = 0;
    backend_calls = 0;
    disconnects = 0;
    block_request = false;
    file_backend = false;
    route_forever = false;
    route_without_service = false;
    file_remaining = 0;
    next_backend_handle = IO_FILESYSTEM_HANDLE_BASE;
}

void tearDown(void)
{
    close_client_channels();
}

void test_open_redirects_to_final_service_and_uses_its_buffers(void)
{
    IOHandle handle = IOClient__open(&bstr("/mnt/ext/file"), IO_OPEN_READ | IO_OPEN_WRITE);

    TEST_ASSERT_GREATER_OR_EQUAL_INT64(IO_SESSION_HANDLE_BASE, handle);
    TEST_ASSERT_EQUAL_UINT(1, route_calls);
    TEST_ASSERT_EQUAL_UINT(1, backend_calls);
    TEST_ASSERT_EQUAL_UINT64(2, IOClientSessions__len(&sessions));
    TEST_ASSERT_TRUE(last_request.buffered);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/mnt/ext/file"), IORequest__path(&last_request));
    TEST_ASSERT_NOT_EQUAL(0, backend_session()->buffers.read.id);
    TEST_ASSERT_NOT_EQUAL(0, backend_session()->buffers.write.id);
}

void test_file_client_combines_buffer_chunks_and_stops_at_eof(void)
{
    file_backend = true;
    file_remaining = BUFFER_SIZE + 300;
    IOHandle handle = IOClient__open(&bstr("/mnt/ext/file"), IO_OPEN_READ | IO_OPEN_WRITE);
    uint8_t * bytes = malloc(BUFFER_SIZE + 400);
    TEST_ASSERT_NOT_NULL(bytes);

    TEST_ASSERT_EQUAL_INT64(BUFFER_SIZE + 300, IOClient__read(handle, bytes, BUFFER_SIZE + 400));
    TEST_ASSERT_EACH_EQUAL_UINT8('f', bytes, BUFFER_SIZE + 300);
    TEST_ASSERT_EQUAL_INT64(0, IOClient__read(handle, bytes, 1));
    TEST_ASSERT_EQUAL_INT64(BUFFER_SIZE + 300, IOClient__write(handle, bytes, BUFFER_SIZE + 300));
    TEST_ASSERT_EQUAL(IO_OK, IOClient__close(handle));
    TEST_ASSERT_EQUAL_UINT64(0, backend_session()->buffers.read.id);
    TEST_ASSERT_EQUAL_UINT64(0, backend_session()->buffers.write.id);
    free(bytes);
}

void test_client_translates_local_handles_for_direct_backend_operations(void)
{
    IOHandle first = IOClient__open(&bstr("socket://one"), IO_OPEN_LISTEN);
    IOHandle second = IOClient__accept(first);
    TEST_ASSERT_GREATER_THAN_INT64(first, second);

    uint8_t bytes[300] = {0};
    TEST_ASSERT_EQUAL_INT64(sizeof(bytes), IOClient__write(second, bytes, sizeof(bytes)));
    TEST_ASSERT_NOT_EQUAL(second, last_request.handle);
    TEST_ASSERT_EQUAL_INT64(2, IOClient__read(second, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_MEMORY("ok", bytes, 2);
    TEST_ASSERT_EQUAL(IO_OK, IOClient__close(second));
    TEST_ASSERT_EQUAL(IO_OK, IOClient__close(first));
    TEST_ASSERT_EQUAL_UINT(1, route_calls);
}

void test_pipe_registers_two_local_handles_with_shared_buffers(void)
{
    IOPipe pipe = unwrap_result(IOClient__pipe(), ((IOPipe){0}));

    TEST_ASSERT_GREATER_OR_EQUAL_INT64(IO_SESSION_HANDLE_BASE, pipe.read);
    TEST_ASSERT_GREATER_THAN_INT64(pipe.read, pipe.write);
    TEST_ASSERT_EQUAL_UINT(1, route_calls);
    TEST_ASSERT_EQUAL_UINT(1, backend_calls);
    TEST_ASSERT_EQUAL(IO_REQUEST_PIPE_CREATE, last_request.operation);
    TEST_ASSERT_TRUE(last_request.buffered);
    TEST_ASSERT_NOT_EQUAL(0, backend_session()->buffers.read.id);
    TEST_ASSERT_NOT_EQUAL(0, backend_session()->buffers.write.id);

    uint8_t bytes[] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT64(sizeof(bytes), IOClient__write(pipe.write, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_INT64(2, IOClient__read(pipe.read, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL(IO_OK, IOClient__close(pipe.read));
    TEST_ASSERT_EQUAL(IO_OK, IOClient__close(pipe.write));
    TEST_ASSERT_EQUAL_UINT64(0, backend_session()->buffers.read.id);
    TEST_ASSERT_EQUAL_UINT64(0, backend_session()->buffers.write.id);
}

void test_client_carries_transfer_flags_on_read_and_write_requests(void)
{
    IOPipe pipe = unwrap_result(IOClient__pipe(), ((IOPipe){0}));
    uint8_t bytes[] = {1, 2, 3};

    TEST_ASSERT_EQUAL_INT64(sizeof(bytes),
                            IOClient__write_with_flags(pipe.write, bytes, sizeof(bytes), IO_TRANSFER_NONBLOCK));
    TEST_ASSERT_EQUAL_UINT64(IO_TRANSFER_NONBLOCK, last_request.flags);
    TEST_ASSERT_EQUAL_INT64(2, IOClient__read_with_flags(pipe.read, bytes, sizeof(bytes), IO_TRANSFER_NONBLOCK));
    TEST_ASSERT_EQUAL_UINT64(IO_TRANSFER_NONBLOCK, last_request.flags);
}

void test_client_transports_control_seek_and_file_change_requests(void)
{
    file_backend = true;
    IOHandle first = IOClient__open(&bstr("/first"), IO_OPEN_READ);
    IOHandle second = IOClient__open(&bstr("/second"), IO_OPEN_READ);

    TEST_ASSERT_EQUAL(IO_OK, IOClient__set_foreground_process(first, 55));
    TEST_ASSERT_EQUAL(IO_REQUEST_SET_FOREGROUND_PROCESS, last_request.operation);
    TEST_ASSERT_EQUAL_UINT64(55, last_request.foreground_pid);

    IOTerminalAttributesResult terminal = IOClient__get_terminal_attributes(first);
    TEST_ASSERT_EQUAL(IO_OK, terminal.status);
    TEST_ASSERT_EQUAL_UINT32(IO_TERMINAL_LOCAL_CANONICAL | IO_TERMINAL_LOCAL_ECHO,
                             terminal.attributes.local_flags & (IO_TERMINAL_LOCAL_CANONICAL | IO_TERMINAL_LOCAL_ECHO));
    terminal.attributes.local_flags &= ~IO_TERMINAL_LOCAL_ECHO;
    TEST_ASSERT_EQUAL(IO_OK, IOClient__set_terminal_attributes(first, terminal.attributes));
    TEST_ASSERT_EQUAL(IO_REQUEST_SET_TERMINAL_ATTRIBUTES, last_request.operation);
    TEST_ASSERT_EQUAL_UINT32(terminal.attributes.local_flags, last_request.terminal_attributes.local_flags);

    IOReply seek = IOClient__seek(first, -12, IO_SEEK_END);
    TEST_ASSERT_EQUAL(IO_OK, seek.status);
    TEST_ASSERT_EQUAL_INT64(-12, (int64_t)seek.value);

    IOReply registered = IOClient__watch_file(first);
    TEST_ASSERT_EQUAL_UINT64(7, registered.file_change.generation);
    TEST_ASSERT_EQUAL_INT64(first, registered.file_change.handle);

    IOFileWatch watches[] = {{.handle = first, .generation = 7}, {.handle = second, .generation = 4}};
    IOReply changed = IOClient__wait_file_change(watches, 2);
    TEST_ASSERT_EQUAL(IO_OK, changed.status);
    TEST_ASSERT_EQUAL_INT64(first, changed.file_change.handle);
    TEST_ASSERT_EQUAL_UINT64(8, changed.file_change.generation);
    TEST_ASSERT_EQUAL_UINT64(IO_FILE_CHANGE_DELETE, changed.file_change.events);
}

void test_service_disconnects_client_with_pending_operation_without_executing_it(void)
{
    IOClient__open(&bstr("socket://one"), IO_OPEN_LISTEN);
    IOClientSessionPtr session = backend_session();
    block_request = true;
    IORequest request = IORequest__new(IO_REQUEST_READ, IO_FILESYSTEM_HANDLE_BASE);
    match_result(Channel__request(&session->channel, (ConstChannelDataPtr)&request, sizeof(request)),
                 success_do_nothing(),
        error_test_fail()
    );
    ChannelFixture__drain();
    QueueConnection__close(Queue__connection(&session->channel.control));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT(1, backend_calls);
    TEST_ASSERT_EQUAL_UINT(1, disconnects);
}

void test_link_redirects_both_paths_to_final_service(void)
{
    TEST_ASSERT_EQUAL(IO_OK, IOClient__link(&bstr("/source"), &bstr("/target")));
    TEST_ASSERT_EQUAL(IO_REQUEST_LINK, last_request.operation);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/source"), IORequest__path(&last_request));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/target"), IORequest__target_path(&last_request));
    TEST_ASSERT_EQUAL_UINT(1, route_calls);
    TEST_ASSERT_EQUAL_UINT(1, backend_calls);
}

void test_server_replaces_wire_identity_with_authenticated_channel_identity(void)
{
    IOClient__open(&bstr("/file"), IO_OPEN_READ);
    IOClientSessionPtr session = backend_session();
    IORequest request = IORequest__new(IO_REQUEST_FILE_INFO, 0);
    request.buffered = true;
    request.client_pid = 999;
    request.client_session = 999;
    request.count = BString__copy_to(&bstr("/file"), (char *)request.data, sizeof(request.data));

    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, IOClient__submit_to(session, &request).status);
    TEST_ASSERT_EQUAL_UINT64(42, last_request.client_pid);
    TEST_ASSERT_NOT_EQUAL(0, last_request.client_session);
    TEST_ASSERT_NOT_EQUAL(999, last_request.client_session);
}

void test_service_rejects_wrong_lease_and_oversized_transfer_before_execution(void)
{
    IOHandle handle = IOClient__open(&bstr("/file"), IO_OPEN_READ);
    IOClientSessionPtr session = backend_session();
    IOClientHandlePtr resource = unwrap_optional(IOClient__find_handle(handle), NULL);
    IORequest request = IORequest__new(IO_REQUEST_READ, resource->backend);
    request.buffered = true;
    request.buffer = session->buffers.read.id + BUFFER_LIMIT;
    request.count = 1;

    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, IOClient__submit_to(session, &request).status);
    request.buffer = session->buffers.read.id;
    request.count = BUFFER_SIZE + 1;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, IOClient__submit_to(session, &request).status);
    TEST_ASSERT_EQUAL_UINT(1, backend_calls);
}

void test_client_rejects_a_route_cycle_after_the_redirect_limit(void)
{
    route_forever = true;

    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, IOClient__open(&bstr("/cycle"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL_UINT(IO_ROUTE_LIMIT + 1, route_calls);
    TEST_ASSERT_EQUAL_UINT(0, backend_calls);
}

void test_client_rejects_a_redirect_without_a_service(void)
{
    route_without_service = true;

    TEST_ASSERT_EQUAL(IO_INVALID_PATH, IOClient__open(&bstr("/empty"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL_UINT(1, route_calls);
    TEST_ASSERT_EQUAL_UINT(0, backend_calls);
}
