#include <stdlib.h>

#include "unity.h"
#include "sys/mem.h"

#define HUND_IO_SYSTEM_NO_MAIN
#include "../../../../../user/services/hund-io-system/main.c"
#include "../../../../../user/lib/io/stream_registry.c"

Result(Anything) mem_alloc_bytes(size_t size)
{
    return Success(Anything, (Anything)malloc(size));
}

MemStatus mem_free_pages(void * address)
{
    free(address);
    return MEM_OK;
}

static IOSystem io_system;

void setUp(void)
{
    io_system = IOSystem__new();
    match_result(IOSystem__register_streams(&io_system, false),
        success_do_nothing(),
        error_test_fail()
    );
}

void tearDown(void)
{
    StreamRegistry__dispose(&io_system.streams);
}

static IOReply execute(IORequest request)
{
    return unwrap_optional(IOSystem__execute(&io_system, 11, 101, &request), IOReply__new(IO_PANIC, 0));
}

static IORequest path_request(IORequestOperation operation, ConstBStringPtr path)
{
    IORequest request = IORequest__new(operation, 0);
    request.flags = IO_OPEN_READ;
    request.count = BString__copy_to(path, (char *)request.data, sizeof(request.data));
    return request;
}

void test_stream_open_routes_directly_to_registered_final_service(void)
{
    IOReply reply = execute(path_request(IO_REQUEST_OPEN, &bstr(":tty:")));

    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_TRUE(reply.route.final);
    TEST_ASSERT_EQUAL_BSTRING(bstr(IO_UART_RX_SERVICE), IORoute__service(&reply.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr(":tty:"), IORoute__path(&reply.route));
}

void test_socket_path_routes_directly_to_final_service(void)
{
    IOReply reply = execute(path_request(IO_REQUEST_OPEN, &bstr("socket://shell")));

    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_TRUE(reply.route.final);
    TEST_ASSERT_EQUAL_BSTRING(bstr(IO_SOCKET_SERVICE), IORoute__service(&reply.route));
}

void test_filesystem_path_routes_to_namespace_before_final_driver(void)
{
    IOReply reply = execute(path_request(IO_REQUEST_FILE_INFO, &bstr("/usr/local/bin/bash")));

    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_FALSE(reply.route.final);
    TEST_ASSERT_EQUAL_BSTRING(bstr(IO_FILESYSTEM_SERVICE), IORoute__service(&reply.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/usr/local/bin/bash"), IORoute__path(&reply.route));
}

void test_filesystem_list_route_preserves_pagination_offset(void)
{
    IORequest request = path_request(IO_REQUEST_LIST_DIR, &bstr("/"));
    request.offset = 7;

    IOReply reply = execute(request);

    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_EQUAL_UINT64(7, reply.value);
    TEST_ASSERT_EQUAL_BSTRING(bstr(IO_FILESYSTEM_SERVICE), IORoute__service(&reply.route));
}

void test_link_preserves_both_paths_for_namespace_routing(void)
{
    IORequest request = path_request(IO_REQUEST_LINK, &bstr("/source"));
    IORequest__set_target_path(&request, &bstr("/target"));
    IOReply reply = execute(request);

    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/source"), IORoute__path(&reply.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/target"), IORoute__target_path(&reply.route));
}

void test_router_rejects_handle_operations(void)
{
    IORequest request = IORequest__new(IO_REQUEST_READ, IO_SESSION_HANDLE_BASE);
    request.count = 1;

    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(request).status);
}

void test_video_mode_routes_console_output_to_video_service(void)
{
    StreamRegistry__dispose(&io_system.streams);
    io_system = IOSystem__new();
    match_result(IOSystem__register_streams(&io_system, true),
        success_do_nothing(),
        error_test_fail()
    );
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr(":console:"));
    request.flags = IO_OPEN_WRITE;
    IOReply reply = execute(request);

    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_EQUAL_BSTRING(bstr(IO_VIDEO_SERVICE), IORoute__service(&reply.route));
}
