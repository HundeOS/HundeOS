#include "unity.h"
#include "../../../../user/drivers/socket_router/sockets.h"
#include "sys/print.h"

static SocketService sockets;

int IO__printf(ConstBStringPtr format, ...)
{
    TEST_FAIL_MESSAGE("Unexpected socket storage failure");
    return 0;
}

void setUp(void)
{
    sockets = SocketService__new();
}

void tearDown(void)
{
}

static IOReply execute(QueueConnectionID session, uint64_t pid, IORequest request)
{
    request.client_session = session;
    request.client_pid = pid;
    return SocketService__execute(&sockets, &request);
}

static IORequest open_request(Flags flags)
{
    IORequest request = IORequest__new(IO_REQUEST_OPEN, 0);
    request.flags = flags;
    request.count = BString__copy_to(&bstr("socket://test"), (char *)request.data, sizeof(request.data));
    return request;
}

static IOHandle open_socket(QueueConnectionID session, uint64_t pid, Flags flags)
{
    IOReply reply = execute(session, pid, open_request(flags));
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    return reply.value;
}

void test_pending_accept_and_read_complete_after_peer_connects_and_writes(void)
{
    IOHandle listener = open_socket(1, 101, IO_OPEN_LISTEN);
    IORequest accept = IORequest__new(IO_REQUEST_ACCEPT, listener);
    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, execute(1, 101, accept).status);

    IOHandle client = open_socket(2, 102, IO_OPEN_READ | IO_OPEN_WRITE);
    IOReply accepted = execute(1, 101, accept);
    TEST_ASSERT_EQUAL(IO_OK, accepted.status);

    IORequest read = IORequest__new(IO_REQUEST_READ, accepted.value);
    read.count = 10;
    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, execute(1, 101, read).status);
    IORequest write = IORequest__new(IO_REQUEST_WRITE, client);
    write.count = 3;
    write.data[0] = 42;
    write.data[1] = 0;
    write.data[2] = 255;
    TEST_ASSERT_EQUAL_INT64(3, execute(2, 102, write).value);
    IOReply reply = execute(1, 101, read);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_INT64(3, reply.value);
    TEST_ASSERT_EQUAL_MEMORY(write.data, reply.data, 3);
}

void test_listener_name_survives_subsequent_request_buffers(void)
{
    IOHandle listener = open_socket(1, 101, IO_OPEN_LISTEN);
    IORequest unrelated = IORequest__new(IO_REQUEST_READ, listener);
    unrelated.count = BString__copy_to(&bstr("not-the-listener"), (char *)unrelated.data, sizeof(unrelated.data));
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, execute(1, 101, unrelated).status);

    TEST_ASSERT_GREATER_OR_EQUAL_INT64(IO_SESSION_HANDLE_BASE, open_socket(2, 102, IO_OPEN_READ | IO_OPEN_WRITE));
}

void test_handles_are_owned_and_protocol_semantics_are_checked(void)
{
    IOHandle listener = open_socket(1, 101, IO_OPEN_LISTEN);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(2, 102, IORequest__new(IO_REQUEST_CLOSE, listener)).status);
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, execute(1, 101, IORequest__new(IO_REQUEST_READ, listener)).status);

    IOHandle client = open_socket(2, 102, IO_OPEN_READ);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, execute(2, 102, IORequest__new(IO_REQUEST_WRITE, client)).status);
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, execute(2, 102, IORequest__new(IO_REQUEST_ACCEPT, client)).status);
    TEST_ASSERT_EQUAL(IO_OK, execute(2, 102, IORequest__new(IO_REQUEST_CLOSE, client)).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(2, 102, IORequest__new(IO_REQUEST_READ, client)).status);
}

void test_peer_close_delivers_buffered_data_then_eof(void)
{
    IOHandle listener = open_socket(1, 101, IO_OPEN_LISTEN);
    IOHandle client = open_socket(2, 102, IO_OPEN_READ | IO_OPEN_WRITE);
    IOHandle peer = execute(1, 101, IORequest__new(IO_REQUEST_ACCEPT, listener)).value;
    IORequest write = IORequest__new(IO_REQUEST_WRITE, client);
    write.count = 1;
    write.data[0] = 99;

    TEST_ASSERT_EQUAL(IO_OK, execute(2, 102, write).status);
    TEST_ASSERT_EQUAL(IO_OK, execute(2, 102, IORequest__new(IO_REQUEST_CLOSE, client)).status);
    IORequest read = IORequest__new(IO_REQUEST_READ, peer);
    read.count = 4;
    TEST_ASSERT_EQUAL_INT64(1, execute(1, 101, read).value);
    TEST_ASSERT_EQUAL_INT64(0, execute(1, 101, read).value);
}

void test_backpressure_reports_wait_without_duplicating_bytes(void)
{
    IOHandle listener = open_socket(1, 101, IO_OPEN_LISTEN);
    IOHandle client = open_socket(2, 102, IO_OPEN_READ | IO_OPEN_WRITE);
    IOHandle peer = execute(1, 101, IORequest__new(IO_REQUEST_ACCEPT, listener)).value;
    IORequest write = IORequest__new(IO_REQUEST_WRITE, client);
    write.count = IO_PROTOCOL_DATA_SIZE;
    for (unsigned index = 0; index < 4; index++)
    {
        TEST_ASSERT_EQUAL_INT64(IO_PROTOCOL_DATA_SIZE, execute(2, 102, write).value);
    }
    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, execute(2, 102, write).status);
    IORequest read = IORequest__new(IO_REQUEST_READ, peer);
    read.count = 7;
    TEST_ASSERT_EQUAL_INT64(7, execute(1, 101, read).value);
    TEST_ASSERT_EQUAL_INT64(7, execute(2, 102, write).value);
}

void test_disconnect_releases_only_session_owned_resources(void)
{
    open_socket(1, 101, IO_OPEN_LISTEN);
    open_socket(2, 102, IO_OPEN_READ | IO_OPEN_WRITE);
    TEST_ASSERT_EQUAL_UINT64(3, SocketResources__len(&sockets.resources));

    SocketService__release(&sockets, 2);
    TEST_ASSERT_EQUAL_UINT64(2, SocketResources__len(&sockets.resources));
    SocketService__release(&sockets, 1);
    TEST_ASSERT_EQUAL_UINT64(0, SocketResources__len(&sockets.resources));
}

void test_missing_identity_and_oversized_requests_are_rejected(void)
{
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, execute(0, 0, open_request(IO_OPEN_LISTEN)).status);
    IOHandle listener = open_socket(1, 101, IO_OPEN_LISTEN);
    IORequest read = IORequest__new(IO_REQUEST_READ, listener);
    read.count = IO_PROTOCOL_DATA_SIZE + 1;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, execute(1, 101, read).status);
}
