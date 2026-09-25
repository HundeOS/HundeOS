#include "unity.h"
#include "ipc/io_diagnostics.h"
#define ServiceDirectory__shared StreamTest__directory
#include "../../../user/lib/io/stream_driver.c"
#include "../../../user/lib/io/stream_registry.c"
#undef ServiceDirectory__shared

static ServiceDirectory directory;
static IOStreamDriver driver;
static IOMediator mediator;
static IORequest pending;
static bool queued;
static bool blocked;
static unsigned transfers;
static StreamRegistry registry;
static unsigned backend_opens;
static BString opened_service;
static bool unavailable;
static IODiagnostics diagnostics;

Result(Channel) Channel__open(ServiceDirectoryPtr directory, ConstBStringPtr service)
{
    if (unavailable)
    {
        return Error(Channel, bstr("Unavailable"), CHANNEL_UNAVAILABLE);
    }
    backend_opens++;
    opened_service = *service;
    return Success(Channel, ((Channel){.token = backend_opens}));
}

ServiceDirectoryPtr StreamTest__directory(void)
{
    return &directory;
}
static IOReply transfer(IOServiceContext context, IORequestPtr request)
{
    if (blocked)
    {
        return IOReply__new(IO_WAIT_FOR_DATA, 0);
    }
    transfers++;
    IOReply reply = IOReply__new(IO_OK, request->count);
    __builtin_memset(reply.data, 'x', request->count);
    return reply;
}
Result(ChannelCode) Channel__request(ChannelPtr self, ConstChannelDataPtr data, size_t size)
{
    TEST_ASSERT_FALSE(queued);
    __builtin_memcpy(&pending, data, size);
    queued = true;
    return Success(ChannelCode, CHANNEL_OK);
}
Result(OptionalChannelReplySize) Channel__poll_reply(ChannelPtr self, ChannelDataPtr data, size_t size)
{
    TEST_ASSERT_TRUE(queued);
    match_optional(
        IOStreamDriver__execute(&driver, 99, 70, &pending),
        some(reply)
        {
            __builtin_memcpy(data, &reply, sizeof(reply));
            queued = false;
            return Success(OptionalChannelReplySize, Some(ChannelReplySize, sizeof(IOReply)));
        },
        none()
        {
            return Success(OptionalChannelReplySize, None(ChannelReplySize));
        }
    );
    return Error(OptionalChannelReplySize, bstr("Missing stream response"), CHANNEL_TRANSPORT_FAILED);
}
ChannelCode Channel__close(ChannelPtr self)
{
    queued = false;
    return CHANNEL_OK;
}
int IO__printf(ConstBStringPtr format, ...)
{
    TEST_FAIL();
    return 0;
}

void setUp(void)
{
    diagnostics = (IODiagnostics){0};
    IODiagnostics__attach(&diagnostics);
    __builtin_memset(&directory, 0, sizeof(directory));
    directory.magic = HUND_SERVICE_DIRECTORY_MAGIC;
    directory.version = HUND_SERVICE_DIRECTORY_VERSION;
    directory.entry_count = 1;
    directory.entries[0].present = 1;
    directory.entries[0].generation = 1;
    directory.entries[0].service_pid = 70;
    directory.entries[0].name = bstr(IO_SYSTEM_SERVICE);
    driver = IOStreamDriver__new(IO_OPEN_WRITE, &directory, transfer);
    mediator = IOMediator__new_stream((Channel){0}, IO_STREAM_HANDLE_BASE);
    queued = false;
    blocked = false;
    transfers = 0;
    registry = StreamRegistry__new();
    backend_opens = 0;
    opened_service = empty_bstr();
    unavailable = false;
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
    request.client_pid = 21;
    request.client_session = 31;
    return request;
}
static IOReply execute(QueueConnectionID session, uint64_t pid, IORequest request)
{
    match_optional(
        IOMediator__execute(&mediator, session, pid, &request),
        some(reply)
        {
            return reply;
        },
        none_test_fail()
    );
    return IOReply__new(IO_PANIC, 0);
}

void test_empty_backend_read_remains_pending_until_data_is_ready(void)
{
    driver = IOStreamDriver__new(IO_OPEN_READ, &directory, transfer);
    IOReply opened = execute(31, 21, open_request(IO_OPEN_READ));
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    IORequest request = IORequest__new(IO_REQUEST_READ, opened.value);
    request.count = 1;
    blocked = true;
    for (unsigned attempt = 0; attempt < 3; attempt++)
    {
        match_optional(IOMediator__execute(&mediator, 31, 21, &request),
            some_test_fail(),
            none_do_nothing()
        );
    }
    IODiagnostics totals = IODiagnostics__snapshot(&diagnostics);
    TEST_ASSERT_TRUE(queued);
    TEST_ASSERT_EQUAL_UINT64(1, totals.backend_reads);
    TEST_ASSERT_EQUAL_UINT64(0, totals.backend_empty);
    blocked = false;
    TEST_ASSERT_EQUAL(IO_OK, execute(31, 21, request).status);
    TEST_ASSERT_FALSE(queued);
    totals = IODiagnostics__snapshot(&diagnostics);
    TEST_ASSERT_EQUAL_UINT64(1, totals.backend_reads);
    TEST_ASSERT_EQUAL_UINT64(0, totals.backend_empty);
}

void test_read_stream_forwards_foreground_process_control(void)
{
    driver = IOStreamDriver__new(IO_OPEN_READ, &directory, transfer);
    IOReply opened = execute(31, 21, open_request(IO_OPEN_READ));
    IORequest request = IORequest__new(IO_REQUEST_SET_FOREGROUND_PROCESS, opened.value);
    request.foreground_pid = 42;

    TEST_ASSERT_EQUAL(IO_OK, execute(31, 21, request).status);
    TEST_ASSERT_EQUAL_UINT(1, transfers);
}

void test_blocked_backend_write_completes_for_mediator_retry(void)
{
    IOReply opened = execute(31, 21, open_request(IO_OPEN_WRITE));
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    IORequest request = IORequest__new(IO_REQUEST_WRITE, opened.value);
    request.count = 1;
    blocked = true;
    match_optional(IOMediator__execute(&mediator, 31, 21, &request),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_FALSE(queued);
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

static IOReply registry_execute(QueueConnectionID session, uint64_t pid, IORequest request)
{
    match_optional(
        StreamRegistry__execute(&registry, session, pid, &request),
        some(reply)
        {
            return reply;
        },
        none_test_fail()
    );
    return IOReply__new(IO_PANIC, 0);
}

void test_registry_routes_arbitrary_names_and_reuses_backend_for_aliases(void)
{
    register_stream(&bstr(":printer:"), IO_OPEN_WRITE, &bstr("print-service"));
    register_stream(&bstr(":console:"), IO_OPEN_WRITE, &bstr("print-service"));
    IOReply first = registry_execute(31, 21, named_open(&bstr(":printer:"), IO_OPEN_WRITE));
    IOReply second = registry_execute(31, 21, named_open(&bstr(":console:"), IO_OPEN_WRITE));
    TEST_ASSERT_EQUAL_INT(IO_OK, first.status);
    TEST_ASSERT_EQUAL_INT(IO_OK, second.status);
    TEST_ASSERT_NOT_EQUAL(first.value, second.value);
    TEST_ASSERT_EQUAL_UINT(1, backend_opens);
    TEST_ASSERT_EQUAL_BSTRING(bstr("print-service"), opened_service);
    IORequest request = IORequest__new(IO_REQUEST_WRITE, first.value);
    request.count = 3;
    TEST_ASSERT_EQUAL_INT(IO_OK, registry_execute(31, 21, request).status);
    TEST_ASSERT_EQUAL_UINT(1, transfers);
    TEST_ASSERT_EQUAL_INT(IO_UNKNOWN_HANDLE, registry_execute(32, 22, request).status);
    TEST_ASSERT_TRUE(StreamRegistry__disconnect(&registry, 31, 21));
    TEST_ASSERT_EQUAL_INT(IO_UNKNOWN_HANDLE, registry_execute(31, 21, request).status);
}

void test_registry_separates_backend_handle_ranges_and_retries_unavailable_service(void)
{
    register_stream(&bstr(":one:"), IO_OPEN_WRITE, &bstr("one"));
    register_stream(&bstr(":two:"), IO_OPEN_WRITE, &bstr("two"));
    unavailable = true;
    TEST_ASSERT_EQUAL_INT(IO_NO_DEVICE_FOUND,
                          registry_execute(31, 21, named_open(&bstr(":one:"), IO_OPEN_WRITE)).status);
    unavailable = false;
    IOReply first = registry_execute(31, 21, named_open(&bstr(":one:"), IO_OPEN_WRITE));
    IOReply second = registry_execute(31, 21, named_open(&bstr(":two:"), IO_OPEN_WRITE));
    TEST_ASSERT_EQUAL_INT(IO_OK, first.status);
    TEST_ASSERT_EQUAL_INT(IO_OK, second.status);
    TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE, second.value - first.value);
    TEST_ASSERT_EQUAL_UINT(2, backend_opens);
    IORequest request = IORequest__new(IO_REQUEST_CLOSE, second.value);
    TEST_ASSERT_EQUAL_INT(IO_OK, registry_execute(31, 21, request).status);
    TEST_ASSERT_EQUAL_INT(IO_UNKNOWN_HANDLE, registry_execute(31, 21, request).status);
}

void test_registry_rejects_unknown_names_access_and_duplicate_registration(void)
{
    register_stream(&bstr(":printer:"), IO_OPEN_WRITE, &bstr("printer"));
    TEST_ASSERT_EQUAL_INT(IO_NO_DEVICE_FOUND,
                          registry_execute(31, 21, named_open(&bstr(":missing:"), IO_OPEN_WRITE)).status);
    TEST_ASSERT_EQUAL_INT(IO_PERMISSION_DENIED,
                          registry_execute(31, 21, named_open(&bstr(":printer:"), IO_OPEN_READ)).status);
    TEST_ASSERT_EQUAL_INT(
        IO_PERMISSION_DENIED,
        registry_execute(31, 21, named_open(&bstr(":printer:"), IO_OPEN_READ | IO_OPEN_WRITE)).status);
    match_result(
        StreamRegistry__register(&registry, &bstr(":printer:"), IO_OPEN_WRITE, &bstr("other")),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL_INT(IO_REGISTER_FAILED, code);
        }
    );
    TEST_ASSERT_EQUAL_UINT(0, backend_opens);
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
    TEST_ASSERT_FALSE(IOPath__is_stream(&bstr(":dev\0ice:")));
    match_result(
        StreamRegistry__register(&registry, &bstr("/printer"), IO_OPEN_WRITE, &bstr("printer")),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code);
        }
    );
    TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH,
                          registry_execute(31, 21, named_open(&bstr(":broken"), IO_OPEN_WRITE)).status);
}

void test_stream_handles_share_session_and_enforce_owner_and_flags(void)
{
    IOReply first = execute(31, 21, open_request(IO_OPEN_WRITE));
    IOReply second = execute(31, 21, open_request(IO_OPEN_WRITE));
    TEST_ASSERT_EQUAL(IO_OK, first.status);
    TEST_ASSERT_NOT_EQUAL(first.value, second.value);
    IORequest request = IORequest__new(IO_REQUEST_WRITE, first.value);
    request.count = 1;
    request.data[0] = 'a';
    request.client_pid = 999;
    TEST_ASSERT_EQUAL(IO_OK, execute(31, 21, request).status);
    TEST_ASSERT_EQUAL(21, pending.client_pid);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(32, 21, request).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(31, 22, request).status);
    request.operation = IO_REQUEST_READ;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, execute(31, 21, request).status);
    TEST_ASSERT_EQUAL(1, transfers);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, execute(31, 21, open_request(IO_OPEN_READ)).status);
}

void test_blocked_stream_does_not_hold_backend_session_or_block_other_opens(void)
{
    IOReply first = execute(31, 21, open_request(IO_OPEN_WRITE));
    IORequest request = IORequest__new(IO_REQUEST_WRITE, first.value);
    request.count = 1;
    blocked = true;
    match_optional(IOMediator__execute(&mediator, 31, 21, &request),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_FALSE(queued);
    TEST_ASSERT_EQUAL(IO_OK, execute(32, 22, open_request(IO_OPEN_WRITE)).status);
    blocked = false;
    TEST_ASSERT_EQUAL(IO_OK, execute(31, 21, request).status);
    TEST_ASSERT_EQUAL(1, transfers);
}

void test_stream_close_and_departure_release_only_owned_handles(void)
{
    IOReply first = execute(31, 21, open_request(IO_OPEN_WRITE));
    IOReply second = execute(32, 22, open_request(IO_OPEN_WRITE));
    TEST_ASSERT_EQUAL(IO_OK, execute(31, 21, IORequest__new(IO_REQUEST_CLOSE, first.value)).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(31, 21, IORequest__new(IO_REQUEST_CLOSE, first.value)).status);
    TEST_ASSERT_EQUAL(1, IOResources__len(&driver.resources));
    TEST_ASSERT_TRUE(IOMediator__disconnect(&mediator, 32, 22));
    TEST_ASSERT_EQUAL(0, IOResources__len(&driver.resources));
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(32, 22, IORequest__new(IO_REQUEST_WRITE, second.value)).status);
}

void test_stream_rejects_forged_mediator_and_oversized_data(void)
{
    IORequest request = open_request(IO_OPEN_WRITE);
    match_optional(
        IOStreamDriver__execute(&driver, 99, 71, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, reply.status);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL(0, IOResources__len(&driver.resources));
    IOReply opened = execute(31, 21, request);
    request = IORequest__new(IO_REQUEST_WRITE, opened.value);
    request.count = IO_PROTOCOL_DATA_SIZE + 1;
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, execute(31, 21, request).status);
    TEST_ASSERT_EQUAL(0, transfers);
    TEST_ASSERT_TRUE(IOStreamDriver__disconnect(&driver, 99, 70));
    TEST_ASSERT_EQUAL(0, IOResources__len(&driver.resources));
}
