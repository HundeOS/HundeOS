#include "../../../hundetest.h"
#include "unity.h"
#include "../../../../user/lib/hunde/native.h"
#include "sys/queue_wait.h"

static HundeAbiResultKind completion_kind;
static uint64_t observed_session;
static uint64_t observed_pid;
static uint64_t observed_capacity;
static uint64_t calls;
static uint64_t observed_token;
static uint64_t observed_target;
static bool observed_buffered;
static uint64_t observed_offset;
static uint64_t completion_count;
static uint64_t completion_name_length;
static bool redirect_completion;
static uint64_t redirect_path_length;
static uint8_t transfer_data[IO_PROTOCOL_DATA_SIZE + 1];
static unsigned local_parks;
static unsigned local_notifications;
static unsigned local_disconnects;

static HundeIOCompletion HundeIOCompletion__new(HundeAbiResultKind kind)
{
    return (HundeIOCompletion){.kind = kind,
                               .value = 2,
                               .pipe_read = IO_SESSION_HANDLE_BASE,
                               .pipe_write = IO_SESSION_HANDLE_BASE + 1,
                               .inheritance_flags = IO_OPEN_WRITE,
                               .file_change = {.handle = IO_SESSION_HANDLE_BASE,
                                               .generation = UINT64_MAX,
                                               .events = IO_FILE_CHANGE_WRITE | IO_FILE_CHANGE_TRUNCATE},
                               .file_info = {.mode = IO_MODE_TYPE_FILE | 0644,
                                             .user_id = 0x100000001ULL,
                                             .group_id = 0x200000002ULL,
                                             .size = 70000,
                                             .identity = 77},
                               .directory_count = completion_count,
                               .directory = {{.info = {.mode = IO_MODE_TYPE_FILE | 0644, .user_id = 21, .group_id = 22},
                                              .name_length = completion_name_length,
                                              .name = {'n', 'a', 'm', 'e'}}},
                               .error = {.code = IO_PERMISSION_DENIED}};
}

static RustServiceCompletion execute_component(HundeComponentContext context, ConstHundeIORequestV6Ptr request)
{
    observed_session = request->session;
    observed_pid = request->pid;
    observed_capacity = request->capacity;
    observed_token = request->inheritance_token;
    observed_target = request->inheritance_pid;
    observed_buffered = request->buffered != 0;
    observed_offset = request->offset;
    if (request->operation == IO_REQUEST_GET_TERMINAL_ATTRIBUTES)
    {
        IOTerminalAttributes attributes = IOTerminalAttributes__default();
        TEST_ASSERT_EQUAL_UINT64(sizeof(attributes), request->count);
        TEST_ASSERT_EQUAL_UINT64(sizeof(attributes), request->capacity);
        __builtin_memcpy(request->read_data, &attributes, sizeof(attributes));
    }
    if (request->operation == IO_REQUEST_SET_TERMINAL_ATTRIBUTES)
    {
        IOTerminalAttributes attributes;
        TEST_ASSERT_EQUAL_UINT64(sizeof(attributes), request->count);
        __builtin_memcpy(&attributes, request->write_data, sizeof(attributes));
        TEST_ASSERT_EQUAL_UINT32(42, attributes.local_flags);
    }
    calls++;
    if (request->operation == IO_REQUEST_READ)
    {
        request->read_data[0] = 0;
        request->read_data[1] = 255;
    }
    if (request->operation == IO_REQUEST_WRITE)
    {
        TEST_ASSERT_EQUAL_UINT8(42, request->write_data[0]);
    }
    if (request->operation == IO_REQUEST_LINK)
    {
        TEST_ASSERT_EQUAL_UINT64(5, request->offset);
        TEST_ASSERT_EQUAL_UINT64(8, request->count);
        TEST_ASSERT_EQUAL_MEMORY("/from/to", request->write_data, 8);
    }
    if (redirect_completion)
    {
        return (RustServiceCompletion){.completion = HundeIOCompletion__new(completion_kind),
                                       .redirect = 1,
                                       .offset = 17,
                                       .service_length = 4,
                                       .path_length = redirect_path_length,
                                       .target_length = 3,
                                       .service = {'/', 't', 'm', 'p'},
                                       .path = {'/', 'i', 'n'},
                                       .target = {'/', 'o', 'u'}};
    }
    return (RustServiceCompletion){.completion = HundeIOCompletion__new(completion_kind)};
}

static HundeStatusResult disconnect_component(HundeComponentContext context, uint64_t session)
{
    local_disconnects++;
    observed_session = session;
    return HundeStatusResult__success();
}

static RustIOCallbacks RustIOCallbacks__new(void)
{
    return (RustIOCallbacks){.execute = execute_component, .disconnect = disconnect_component};
}

void setUp(void)
{
    completion_kind = HUNDE_ABI_SUCCESS;
    redirect_completion = false;
    redirect_path_length = 3;
    observed_session = 0;
    observed_pid = 0;
    observed_capacity = 0;
    calls = 0;
    completion_count = 0;
    completion_name_length = 4;
    local_parks = local_notifications = local_disconnects = 0;
    QueueWait__swap_adapter(&None(QueueWaitAdapter));
}

void tearDown(void)
{
}

static Result(QueueWaitCode) local_park(Anything context)
{
    local_parks++;
    if (local_parks == 2)
    {
        completion_kind = HUNDE_ABI_SUCCESS;
    }
    return Success(QueueWaitCode, QUEUE_WAIT_CHANGED);
}

static void local_notify(Anything context)
{
    local_notifications++;
}

void test_local_request_retries_pending_on_the_cooperative_scheduler(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    QueueWaitAdapter adapter = QueueWaitAdapter__new(0, local_park, local_notify);
    QueueWait__swap_adapter(&Some(QueueWaitAdapter, adapter));
    completion_kind = HUNDE_IO_PENDING;
    IORequest request = IORequest__new(IO_REQUEST_SEEK, 123);
    IOReply reply = RustIOCallbacks__execute_local(&component, 456, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_UINT64(3, calls);
    TEST_ASSERT_EQUAL_UINT64(HUNDE_LOCAL_IO_SESSION, observed_session);
    TEST_ASSERT_EQUAL_UINT64(456, observed_pid);
    TEST_ASSERT_EQUAL_UINT(2, local_parks);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(3, local_notifications);
    TEST_ASSERT_EQUAL_UINT(0, local_disconnects);
    QueueWait__swap_adapter(&None(QueueWaitAdapter));
}

void test_local_request_without_a_scheduler_cancels_pending_and_releases_its_gate(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    completion_kind = HUNDE_IO_PENDING;
    IORequest request = IORequest__new(IO_REQUEST_SEEK, 123);
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, RustIOCallbacks__execute_local(&component, 456, &request).status);
    TEST_ASSERT_EQUAL_UINT(1, local_disconnects);
    completion_kind = HUNDE_ABI_ERROR;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, RustIOCallbacks__execute_local(&component, 456, &request).status);
    completion_kind = HUNDE_ABI_SUCCESS;
    TEST_ASSERT_EQUAL(IO_OK, RustIOCallbacks__execute_local(&component, 456, &request).status);
}

void test_namespace_redirect_preserves_paths_and_directory_offset(void)
{
    redirect_completion = true;
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_LIST_DIR, 0);
    match_optional(
        RustIOCallbacks__execute(&component, 1, 2, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
            TEST_ASSERT_EQUAL_UINT64(17, reply.value);
            TEST_ASSERT_EQUAL_BSTRING(bstr("/tmp"), IORoute__service(&reply.route));
            TEST_ASSERT_EQUAL_BSTRING(bstr("/in"), IORoute__path(&reply.route));
            TEST_ASSERT_EQUAL_BSTRING(bstr("/ou"), IORoute__target_path(&reply.route));
        },
        none_test_fail()
    );
}

void test_namespace_redirect_rejects_oversized_paths_and_failed_completions(void)
{
    redirect_completion = true;
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_OPEN, 0);
    redirect_path_length = IO_PROTOCOL_DATA_SIZE;
    match_optional(
        RustIOCallbacks__execute(&component, 1, 2, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_PANIC, reply.status);
        },
        none_test_fail()
    );
    redirect_path_length = 3;
    completion_kind = HUNDE_ABI_ERROR;
    match_optional(
        RustIOCallbacks__execute(&component, 1, 2, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_PANIC, reply.status);
        },
        none_test_fail()
    );
}

void test_watch_completions_preserve_handle_generation_and_events(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_WATCH_FILE, IO_SESSION_HANDLE_BASE);
    match_optional(
        RustIOCallbacks__execute(&component, 123, 456, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_OK, reply.status);
            TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE, reply.file_change.handle);
            TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, reply.file_change.generation);
            TEST_ASSERT_EQUAL_UINT64(IO_FILE_CHANGE_WRITE | IO_FILE_CHANGE_TRUNCATE, reply.file_change.events);
        },
        none_test_fail()
    );
    request.operation = IO_REQUEST_WAIT_FILE_CHANGE;
    completion_kind = HUNDE_IO_PENDING;
    match_optional(RustIOCallbacks__execute(&component, 123, 456, &request),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_authenticated_metadata_and_inline_binary_reply(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_READ, IO_SESSION_HANDLE_BASE);
    request.count = 2;
    request.client_pid = 900;
    request.client_session = 901;
    match_optional(
        RustIOCallbacks__execute(&component, 123, 456, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_OK, reply.status);
            TEST_ASSERT_EQUAL_INT64(2, reply.value);
            TEST_ASSERT_EQUAL_UINT8(0, reply.data[0]);
            TEST_ASSERT_EQUAL_UINT8(255, reply.data[1]);
        },
        none()
        {
            TEST_FAIL_MESSAGE("Expected completion");
        }
    );
    TEST_ASSERT_EQUAL_UINT64(123, observed_session);
    TEST_ASSERT_EQUAL_UINT64(456, observed_pid);
    TEST_ASSERT_EQUAL_UINT64(IO_PROTOCOL_DATA_SIZE, observed_capacity);
}

void test_pending_is_not_an_error_reply(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_ACCEPT, IO_SESSION_HANDLE_BASE);
    completion_kind = HUNDE_IO_PENDING;
    TEST_ASSERT_FALSE(is_something(RustIOCallbacks__execute(&component, 1, 2, &request)));
}

void test_pipe_and_inheritance_fields_cross_component_boundary(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_PIPE_CREATE, 0);
    request.buffered = true;
    request.inheritance_token = 123;
    request.inheritance_pid = 456;
    match_optional(
        RustIOCallbacks__execute(&component, 1, 2, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE, reply.pipe.read);
            TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE + 1, reply.pipe.write);
            TEST_ASSERT_EQUAL_UINT64(IO_OPEN_WRITE, reply.inheritance_flags);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(123, observed_token);
    TEST_ASSERT_EQUAL_UINT64(456, observed_target);
    TEST_ASSERT_TRUE(observed_buffered);
}

void test_component_error_becomes_protocol_error(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_CLOSE, IO_SESSION_HANDLE_BASE);
    completion_kind = HUNDE_ABI_ERROR;
    match_optional(
        RustIOCallbacks__execute(&component, 1, 2, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, reply.status);
        },
        none()
        {
            TEST_FAIL_MESSAGE("Expected error reply");
        }
    );
}

void test_oversized_request_never_reaches_module(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_WRITE, IO_SESSION_HANDLE_BASE);
    request.count = IO_PROTOCOL_DATA_SIZE + 1;
    match_optional(
        RustIOCallbacks__execute(&component, 1, 2, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, reply.status);
        },
        none()
        {
            TEST_FAIL_MESSAGE("Expected error reply");
        }
    );
    TEST_ASSERT_EQUAL_UINT64(0, calls);
}

void test_managed_transfer_address_is_forwarded(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_WRITE, IO_SESSION_HANDLE_BASE);
    transfer_data[0] = 42;
    request.transfer_address = (Address)transfer_data;
    request.count = sizeof(transfer_data);
    TEST_ASSERT_TRUE(is_something(RustIOCallbacks__execute(&component, 1, 2, &request)));
    TEST_ASSERT_EQUAL_UINT64(BUFFER_SIZE, observed_capacity);
    TEST_ASSERT_TRUE(RustIOCallbacks__disconnect(&component, 777, 2));
    TEST_ASSERT_EQUAL_UINT64(777, observed_session);
}

void test_filesystem_offset_and_metadata_are_translated_without_narrowing_ids(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_FILE_INFO, 0);
    request.offset = 123;
    completion_count = 1;
    match_optional(
        RustIOCallbacks__execute(&component, 1, 2, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_OK, reply.status);
            TEST_ASSERT_EQUAL_UINT64(0x100000001ULL, reply.file_info.user_id);
            TEST_ASSERT_EQUAL_UINT64(0x200000002ULL, reply.file_info.group_id);
            TEST_ASSERT_EQUAL_UINT64(70000, reply.file_info.size);
            TEST_ASSERT_EQUAL_UINT64(77, reply.file_info.identity);
        },
        none_test_fail()
    );
    request.operation = IO_REQUEST_LIST_DIR;
    match_optional(
        RustIOCallbacks__execute(&component, 1, 2, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL_UINT64(1, reply.directory.count);
            TEST_ASSERT_EQUAL_UINT64(21, reply.directory.entries[0].user_id);
            TEST_ASSERT_EQUAL_UINT64(22, reply.directory.entries[0].group_id);
            TEST_ASSERT_EQUAL_UINT64(4, reply.directory.entries[0].name_len);
            TEST_ASSERT_EQUAL_MEMORY("name", reply.directory.entries[0].name, 4);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(123, observed_offset);
}

void test_invalid_directory_batch_or_name_is_rejected(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_LIST_DIR, 0);
    completion_count = IO_DIRECTORY_BATCH_CAPACITY + 1;
    match_optional(
        RustIOCallbacks__execute(&component, 1, 2, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_PANIC, reply.status);
        },
        none_test_fail()
    );
    completion_count = 1;
    completion_name_length = IO_DIRENT_NAME_MAX;
    match_optional(
        RustIOCallbacks__execute(&component, 1, 2, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_PANIC, reply.status);
        },
        none_test_fail()
    );
}

void test_terminal_attributes_and_foreground_use_bounded_component_fields(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_GET_TERMINAL_ATTRIBUTES, 7);
    match_optional(
        RustIOCallbacks__execute(&component, 123, 456, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_OK, reply.status);
            IOTerminalAttributes expected = IOTerminalAttributes__default();
            TEST_ASSERT_EQUAL_MEMORY(&expected, &reply.terminal_attributes, sizeof(expected));
        },
        none_test_fail()
    );
    request.operation = IO_REQUEST_SET_TERMINAL_ATTRIBUTES;
    request.terminal_attributes.local_flags = 42;
    match_optional(
        RustIOCallbacks__execute(&component, 123, 456, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_OK, reply.status);
        },
        none_test_fail()
    );
    request.operation = IO_REQUEST_SET_FOREGROUND_PROCESS;
    request.foreground_pid = 987;
    match_optional(
        RustIOCallbacks__execute(&component, 123, 456, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_OK, reply.status);
            TEST_ASSERT_EQUAL_UINT64(987, observed_offset);
            TEST_ASSERT_EQUAL_UINT64(123, observed_session);
            TEST_ASSERT_EQUAL_UINT64(456, observed_pid);
        },
        none_test_fail()
    );
}

void test_link_paths_are_bounded_packed_and_authenticated(void)
{
    RustIOCallbacks component = RustIOCallbacks__new();
    IORequest request = IORequest__new(IO_REQUEST_LINK, 0);
    request.count = 5;
    __builtin_memcpy(request.data, "/from", 5);
    IORequest__set_target_path(&request, &bstr("/to"));
    match_optional(
        RustIOCallbacks__execute(&component, 123, 456, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_OK, reply.status);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(123, observed_session);
    TEST_ASSERT_EQUAL_UINT64(456, observed_pid);
    TEST_ASSERT_EQUAL_UINT64(2 * IO_PROTOCOL_DATA_SIZE, observed_capacity);
    for (uint64_t length = IO_PROTOCOL_DATA_SIZE; length <= IO_PROTOCOL_DATA_SIZE + 1; length++)
    {
        request.target_path_length = length;
        match_optional(
            RustIOCallbacks__execute(&component, 1, 2, &request),
            some(reply)
            {
                TEST_ASSERT_EQUAL(IO_INVALID_PATH, reply.status);
            },
            none_test_fail()
        );
    }
    TEST_ASSERT_EQUAL_UINT64(1, calls);
}
