#include <stdlib.h>

#include "unity.h"
#include "ipc/io_diagnostics.h"
#include "sys/mem.h"
#include "../../../user/lib/io/stream_driver.c"
#include "../../../user/lib/io/stream_registry.c"

static IOStreamDriver driver;
static StreamRegistry registry;
static bool blocked;
static unsigned transfers;
static IORequest last_transfer;
static IODiagnostics diagnostics;

Result(Anything) mem_alloc_bytes(size_t size)
{
    return Success(Anything, (Anything)malloc(size));
}

static IOReply transfer(IOServiceContext context, IORequestPtr request)
{
    last_transfer = *request;
    if (blocked)
    {
        return IOReply__new(IO_WAIT_FOR_DATA, 0);
    }
    transfers++;
    IOReply reply = IOReply__new(IO_OK, request->count);
    __builtin_memset(IORequest__read_target(request, &reply), 'x', request->count);
    return reply;
}

void setUp(void)
{
    diagnostics = (IODiagnostics){0};
    IODiagnostics__attach(&diagnostics);
    driver = IOStreamDriver__new(IO_OPEN_WRITE, 0, transfer);
    registry = StreamRegistry__new();
    blocked = false;
    transfers = 0;
    last_transfer = (IORequest){0};
}

void tearDown(void)
{
    StreamRegistry__dispose(&registry);
}

static IORequest open_request(Flags flags)
{
    IORequest request = IORequest__new(IO_REQUEST_OPEN, 0);
    request.count = BString__copy_to(&bstr(":console:"), (char *)request.data, sizeof(request.data));
    request.flags = flags;
    return request;
}

static Optional(IOReply) execute_optional(QueueConnectionID session, uint64_t pid, IORequest request)
{
    request.client_pid = pid;
    request.client_session = session;
    return IOStreamDriver__execute(&driver, session, pid, &request);
}

static IOReply execute(QueueConnectionID session, uint64_t pid, IORequest request)
{
    return unwrap_optional(execute_optional(session, pid, request), IOReply__new(IO_PANIC, 0));
}

static IOHandle open_stream(QueueConnectionID session, uint64_t pid, Flags flags)
{
    IOReply reply = execute(session, pid, open_request(flags));
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    return reply.value;
}

static void register_stream(ConstBStringPtr name, Flags access, ConstBStringPtr service)
{
    match_result(StreamRegistry__register(&registry, name, access, service),
        success_do_nothing(),
        error_test_fail()
    );
}

static IORequest named_open(ConstBStringPtr name, Flags access)
{
    IORequest request = open_request(access);
    request.count = BString__copy_to(name, (char *)request.data, sizeof(request.data));
    return request;
}

void test_empty_read_remains_pending_until_data_is_ready(void)
{
    driver = IOStreamDriver__new(IO_OPEN_READ, 0, transfer);
    IOHandle handle = open_stream(31, 21, IO_OPEN_READ);
    IORequest request = IORequest__new(IO_REQUEST_READ, handle);
    request.count = 1;
    blocked = true;

    TEST_ASSERT_TRUE(is_nothing(execute_optional(31, 21, request)));
    TEST_ASSERT_EQUAL_UINT(0, transfers);
    blocked = false;
    TEST_ASSERT_EQUAL(IO_OK, execute(31, 21, request).status);
    TEST_ASSERT_EQUAL_UINT(1, transfers);
}

void test_blocked_write_returns_provider_status_without_holding_global_state(void)
{
    IOHandle first = open_stream(31, 21, IO_OPEN_WRITE);
    IORequest request = IORequest__new(IO_REQUEST_WRITE, first);
    request.count = 1;
    blocked = true;

    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, execute(31, 21, request).status);
    TEST_ASSERT_EQUAL(IO_OK, execute(32, 22, open_request(IO_OPEN_WRITE)).status);
}

void test_read_stream_forwards_foreground_process_control(void)
{
    driver = IOStreamDriver__new(IO_OPEN_READ, 0, transfer);
    IOHandle handle = open_stream(31, 21, IO_OPEN_READ);
    IORequest request = IORequest__new(IO_REQUEST_SET_FOREGROUND_PROCESS, handle);
    request.foreground_pid = 42;

    TEST_ASSERT_EQUAL(IO_OK, execute(31, 21, request).status);
    TEST_ASSERT_EQUAL_UINT(1, transfers);
    TEST_ASSERT_EQUAL_UINT64(42, last_transfer.foreground_pid);
}

void test_registry_returns_final_routes_for_registered_streams(void)
{
    register_stream(&bstr(":printer:"), IO_OPEN_WRITE, &bstr("print-service"));
    register_stream(&bstr(":console:"), IO_OPEN_WRITE, &bstr("print-service"));
    IORequest printer = named_open(&bstr(":printer:"), IO_OPEN_WRITE);
    IORequest console = named_open(&bstr(":console:"), IO_OPEN_WRITE);

    IOReply first = StreamRegistry__route(&registry, &printer);
    IOReply second = StreamRegistry__route(&registry, &console);

    TEST_ASSERT_EQUAL(IO_REDIRECT, first.status);
    TEST_ASSERT_EQUAL(IO_REDIRECT, second.status);
    TEST_ASSERT_TRUE(first.route.final);
    TEST_ASSERT_EQUAL_BSTRING(bstr("print-service"), IORoute__service(&first.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr(":printer:"), IORoute__path(&first.route));
}

void test_registry_rejects_unknown_names_access_and_duplicate_registration(void)
{
    register_stream(&bstr(":printer:"), IO_OPEN_WRITE, &bstr("printer"));
    IORequest missing = named_open(&bstr(":missing:"), IO_OPEN_WRITE);
    IORequest denied = named_open(&bstr(":printer:"), IO_OPEN_READ);

    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, StreamRegistry__route(&registry, &missing).status);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, StreamRegistry__route(&registry, &denied).status);
    match_result(
        StreamRegistry__register(&registry, &bstr(":printer:"), IO_OPEN_WRITE, &bstr("other")),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(IO_REGISTER_FAILED, code);
        }
    );
    register_stream(&bstr(":printer:"), IO_OPEN_READ, &bstr("input"));
}

void test_registry_validates_stream_syntax(void)
{
    TEST_ASSERT_TRUE(IOPath__is_stream(&bstr(":device-1:")));
    TEST_ASSERT_FALSE(IOPath__is_stream(&bstr("::")));
    TEST_ASSERT_FALSE(IOPath__is_stream(&bstr(":device")));
    TEST_ASSERT_FALSE(IOPath__is_stream(&bstr("device:")));
    TEST_ASSERT_FALSE(IOPath__is_stream(&bstr(":dev:ice:")));
    TEST_ASSERT_FALSE(IOPath__is_stream(&bstr(":dev/ice:")));
    TEST_ASSERT_FALSE(IOPath__is_stream(&bstr(":dev ice:")));
    match_result(
        StreamRegistry__register(&registry, &bstr("/printer"), IO_OPEN_WRITE, &bstr("printer")),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(IO_INVALID_PATH, code);
        }
    );
}

void test_registry_owns_registered_name_and_service(void)
{
    char name_bytes[] = ":printer:";
    char service_bytes[] = "printer";
    BString name = BString__from_chars(name_bytes, sizeof(name_bytes) - 1);
    BString service = BString__from_chars(service_bytes, sizeof(service_bytes) - 1);
    register_stream(&name, IO_OPEN_WRITE, &service);

    name_bytes[1] = 'x';
    service_bytes[0] = 'x';
    IORequest request = named_open(&bstr(":printer:"), IO_OPEN_WRITE);
    IOReply reply = StreamRegistry__route(&registry, &request);

    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_EQUAL_BSTRING(bstr("printer"), IORoute__service(&reply.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr(":printer:"), IORoute__path(&reply.route));
}

void test_stream_handles_enforce_authenticated_owner_and_access(void)
{
    IOHandle first = open_stream(31, 21, IO_OPEN_WRITE);
    IOHandle second = open_stream(31, 21, IO_OPEN_WRITE);
    TEST_ASSERT_NOT_EQUAL(first, second);
    IORequest request = IORequest__new(IO_REQUEST_WRITE, first);
    request.count = 1;

    TEST_ASSERT_EQUAL(IO_OK, execute(31, 21, request).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(32, 21, request).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(31, 22, request).status);
    request.operation = IO_REQUEST_READ;
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, execute(31, 21, request).status);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, execute(31, 21, open_request(IO_OPEN_READ)).status);
}

void test_close_and_disconnect_release_only_owned_handles(void)
{
    IOHandle first = open_stream(31, 21, IO_OPEN_WRITE);
    IOHandle second = open_stream(32, 22, IO_OPEN_WRITE);

    TEST_ASSERT_EQUAL(IO_OK, execute(31, 21, IORequest__new(IO_REQUEST_CLOSE, first)).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(31, 21, IORequest__new(IO_REQUEST_CLOSE, first)).status);
    TEST_ASSERT_EQUAL_UINT64(1, IOStreamResources__len(&driver.resources));
    TEST_ASSERT_TRUE(IOStreamDriver__disconnect(&driver, 32, 22));
    TEST_ASSERT_EQUAL_UINT64(0, IOStreamResources__len(&driver.resources));
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(32, 22, IORequest__new(IO_REQUEST_WRITE, second)).status);
}

void test_stream_rejects_missing_identity_and_oversized_data(void)
{
    IORequest request = open_request(IO_OPEN_WRITE);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, execute(0, 0, request).status);
    TEST_ASSERT_EQUAL_UINT64(0, IOStreamResources__len(&driver.resources));

    IOHandle handle = open_stream(31, 21, IO_OPEN_WRITE);
    request = IORequest__new(IO_REQUEST_WRITE, handle);
    request.count = IO_PROTOCOL_DATA_SIZE + 1;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, execute(31, 21, request).status);
    TEST_ASSERT_EQUAL_UINT(0, transfers);
}
