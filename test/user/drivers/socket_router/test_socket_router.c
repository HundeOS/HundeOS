#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "../../../../common/types/result.h"
#include "../../../../common_includes/syscalls/service.h"
#include "../../../../common_includes/syscalls/socket.h"
#include "../../../../common_includes/syscalls/syslog.h"

#define main socket_router_main_under_test
#include "../../../../user/drivers/socket_router/main.c"
#undef main

static bool fake_became_service;
static bool fake_socket_router_registered;
static bool fake_ready_published;
static BString fake_ready_name;
static bool fake_async_started;
static IOSocketRouterRequest fake_socket_request;
static IOSocketRouterCompletion fake_socket_completion;
static AsyncIORegisterService fake_register_service;
static AsyncIORequestService fake_request_service;
static Anything fake_service_context;

static Result(AsyncIOCode) fake_unset_register_service(Anything service_context)
{
    TEST_FAIL_MESSAGE("AsyncIO register service was not captured");
    return Error(AsyncIOCode, bstr("unset register service"), ASYNC_IO_REGISTER_FAILED);
}

static Result(FiberCode) fake_unset_request_service(FiberPtr fiber, Anything service_context, ServiceEvent request)
{
    TEST_FAIL_MESSAGE("AsyncIO request service was not captured");
    return Error(FiberCode, bstr("unset request service"), FIBER_INVALID_STATE);
}

void setUp(void)
{
    fake_became_service = false;
    fake_socket_router_registered = false;
    fake_ready_published = false;
    fake_ready_name = bstr("");
    fake_async_started = false;
    fake_socket_request = IOSocketRouterRequest__new(0, IO_SOCKET_ROUTER_LISTEN, &bstr(""), 0, 0);
    fake_socket_completion = IOSocketRouterCompletion__new(0, IO_SOCKET_INVALID_URI, -1);
    fake_register_service = fake_unset_register_service;
    fake_request_service = fake_unset_request_service;
    fake_service_context = 0;
}

void tearDown(void)
{
}

SyslogResult kernel_syslogf(const char * format, ...)
{
    return SYSLOG_OK;
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
}

ServiceCode service_become_service(void)
{
    fake_became_service = true;
    return SERVICE_OK;
}

ServiceCode service_register_socket_router(void)
{
    fake_socket_router_registered = true;
    return SERVICE_OK;
}

ServiceCode service_socket_get_request(uint64_t request_id, IOSocketRouterRequestPtr request)
{
    TEST_ASSERT_EQUAL_UINT64(fake_socket_request.request_id, request_id);
    *request = fake_socket_request;
    return SERVICE_OK;
}

ServiceCode service_socket_complete(uint64_t request_id, IOSocketStatus status, IOHandle result)
{
    fake_socket_completion = IOSocketRouterCompletion__new(request_id, status, result);
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    fake_ready_published = true;
    fake_ready_name = *name;
    return SERVICE_OK;
}

Result(AsyncIOCode) AsyncIO__start(
    AsyncIOPtr self,
    AsyncIORegisterService register_service,
    Anything service_context,
    AsyncIORequestService request_service
)
{
    fake_async_started = true;
    fake_register_service = register_service;
    fake_request_service = request_service;
    fake_service_context = service_context;
    return register_service(service_context);
}

void test_socket_router_rejects_non_socket_uri(void)
{
    SocketRouter router = SocketRouter__new();
    BString uri = bstr("/tmp/sock1");

    match_result(IOSocketStatus, SocketRouter__listen(&router, &bstr("/tmp/sock1"), 10, 4),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_SOCKET_INVALID_URI, code); }
    );
}

void test_socket_router_listens_and_accepts_pending_connection(void)
{
    SocketRouter router = SocketRouter__new();
    BString uri = bstr("socket://proc/sock1");

    match_result(IOSocketStatus, SocketRouter__listen(&router, &bstr("socket://proc/sock1"), 10, 4),
        success(status) { TEST_ASSERT_EQUAL(IO_SOCKET_OK, status); },
        error_test_fail()
    );

    match_result(IOHandle, SocketRouter__connect(&router, &bstr("socket://proc/sock1"), 20),
        success(listener) { TEST_ASSERT_EQUAL_INT64(10, listener); },
        error_test_fail()
    );

    match_result(IOHandle, SocketRouter__accept(&router, 10),
        success(handle) { TEST_ASSERT_EQUAL_INT64(20, handle); },
        error_test_fail()
    );
}

void test_socket_router_enforces_listener_backlog(void)
{
    SocketRouter router = SocketRouter__new();
    BString uri = bstr("socket://proc/sock1");

    match_result(IOSocketStatus, SocketRouter__listen(&router, &bstr("socket://proc/sock1"), 10, 1),
        success(status) { TEST_ASSERT_EQUAL(IO_SOCKET_OK, status); },
        error_test_fail()
    );

    match_result(IOHandle, SocketRouter__connect(&router, &bstr("socket://proc/sock1"), 20),
        success(listener) { TEST_ASSERT_EQUAL_INT64(10, listener); },
        error_test_fail()
    );

    match_result(IOHandle, SocketRouter__connect(&router, &bstr("socket://proc/sock1"), 21),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_SOCKET_BACKLOG_FULL, code); }
    );
}

void test_socket_router_main_starts_async_io_and_registers_service(void)
{
    TEST_ASSERT_EQUAL_INT(0, socket_router_main_under_test());
    TEST_ASSERT_TRUE(fake_async_started);
    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_socket_router_registered);
    TEST_ASSERT_TRUE(fake_ready_published);
    TEST_ASSERT_EQUAL_STRING(SOCKET_ROUTER_READY_NAME, BString__cstr(&fake_ready_name));
}

void test_socket_router_async_request_service_handles_socket_request(void)
{
    SocketRouter router = SocketRouter__new();
    IOSocketRouterRequest listen_request = IOSocketRouterRequest__new(1, IO_SOCKET_ROUTER_LISTEN, &bstr("socket://proc/sock1"), 10, 4);
    IOSocketRouterRequest connect_request = IOSocketRouterRequest__new(2, IO_SOCKET_ROUTER_CONNECT, &bstr("socket://proc/sock1"), 20, 0);
    ServiceEvent listen_event = ServiceEvent__request(SERVICE_EVENT_SOCKET_IO, 1, 0);
    ServiceEvent connect_event = ServiceEvent__request(SERVICE_EVENT_SOCKET_IO, 2, 0);

    fake_socket_request = listen_request;
    match_result(FiberCode, SocketRouter__request_service((FiberPtr) 1, (Anything) &router, listen_event),
        success(code) { TEST_ASSERT_EQUAL(FIBER_OK, code); },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(IO_SOCKET_OK, fake_socket_completion.status);
    TEST_ASSERT_EQUAL_INT64(10, fake_socket_completion.result);

    fake_socket_request = connect_request;
    match_result(FiberCode, SocketRouter__request_service((FiberPtr) 1, (Anything) &router, connect_event),
        success(code) { TEST_ASSERT_EQUAL(FIBER_OK, code); },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(IO_SOCKET_OK, fake_socket_completion.status);
    TEST_ASSERT_EQUAL_INT64(10, fake_socket_completion.result);

    match_result(IOHandle, SocketRouter__accept(&router, 10),
        success(handle) { TEST_ASSERT_EQUAL_INT64(20, handle); },
        error_test_fail()
    );
}
