#include "unity.h"
#include "../../../../user/drivers/socket_router/sockets.h"
#include "io/mediator.h"
#include "sys/print.h"

static SocketService sockets;
static IOMediator mediator;
static IORequest pending;
static bool queued;
static bool delay;
static bool broken;
static bool deferred_open;
static unsigned requests;

Result(ChannelCode) Channel__request(ChannelPtr self, ConstChannelDataPtr data, size_t size)
{
    TEST_ASSERT_FALSE(queued);
    TEST_ASSERT_EQUAL_UINT64(sizeof(pending), size);
    if (broken)
    {
        return Error(ChannelCode, bstr("backend died"), CHANNEL_UNAVAILABLE);
    }
    __builtin_memcpy(&pending, data, size);
    queued = true;
    delay = true;
    requests++;
    return Success(ChannelCode, CHANNEL_OK);
}

Result(OptionalChannelReplySize) Channel__poll_reply(ChannelPtr self, ChannelDataPtr data, size_t size)
{
    TEST_ASSERT_TRUE(queued);
    if (broken)
    {
        return Error(OptionalChannelReplySize, bstr("backend died"), CHANNEL_UNAVAILABLE);
    }
    if (delay)
    {
        delay = false;
        return Success(OptionalChannelReplySize, None(ChannelReplySize));
    }
    IOReply reply = deferred_open && pending.operation == IO_REQUEST_OPEN ? IOReply__new(IO_WAIT_FOR_DATA, 0)
                                                                          : SocketService__execute(&sockets, &pending);
    __builtin_memcpy(data, &reply, sizeof(reply));
    queued = false;
    return Success(OptionalChannelReplySize, Some(ChannelReplySize, sizeof(reply)));
}

ChannelCode Channel__close(ChannelPtr self)
{
    queued = false;
    sockets.resources = SocketResources__new();
    return CHANNEL_OK;
}

int IO__printf(ConstBStringPtr format, ...)
{
    TEST_FAIL_MESSAGE("Unexpected socket storage failure");
    return 0;
}

void setUp(void)
{
    sockets = SocketService__new();
    mediator = IOMediator__new((Channel){0});
    queued = false;
    delay = false;
    broken = false;
    deferred_open = false;
    requests = 0;
}
void tearDown(void)
{
}

void test_disconnect_releases_deferred_open_after_transport_reply_was_consumed(void)
{
    IORequest request = IORequest__new(IO_REQUEST_OPEN, 0);
    request.count = BString__copy_to(&bstr("socket://deferred"), (char *)request.data, sizeof(request.data));
    request.flags = IO_OPEN_READ;
    deferred_open = true;
    match_optional(IOMediator__execute(&mediator, 1, 101, &request),
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(IOMediator__execute(&mediator, 1, 101, &request),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_EQUAL(0, mediator.active);
    TEST_ASSERT_EQUAL(1, IOPendingSessions__len(&mediator.pending_opens));
    bool done = false;
    for (unsigned retry = 0; retry < 10 && !done; retry++)
    {
        done = IOMediator__disconnect(&mediator, 1, 101);
    }
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_EQUAL(IO_REQUEST_RELEASE_CLIENT, pending.operation);
    TEST_ASSERT_EQUAL(2, requests);
    TEST_ASSERT_EQUAL(0, IOPendingSessions__len(&mediator.pending_opens));
}

static IOReply execute(uint64_t client, IORequest request)
{
    for (unsigned retry = 0; retry < 20; retry++)
    {
        match_optional(
            IOMediator__execute(&mediator, client, client + 100, &request),
            some(reply) { return reply; },
            none_do_nothing()
        );
    }
    TEST_FAIL_MESSAGE("IO operation did not complete");
    return IOReply__new(IO_PANIC, 0);
}

static IORequest open_request(Flags flags)
{
    IORequest request = IORequest__new(IO_REQUEST_OPEN, 0);
    request.flags = flags;
    request.count = BString__copy_to(&bstr("socket://test"), (char *)request.data, sizeof(request.data));
    return request;
}

static IOHandle open_socket(uint64_t client, Flags flags)
{
    IOReply reply = execute(client, open_request(flags));
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    return reply.value;
}

void test_pending_accept_and_read_allow_other_clients_to_connect_and_write(void)
{
    IOHandle listener = open_socket(1, IO_OPEN_LISTEN);
    IORequest accept_request = IORequest__new(IO_REQUEST_ACCEPT, listener);
    match_optional(IOMediator__execute(&mediator, 1, 101, &accept_request),
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(IOMediator__execute(&mediator, 1, 101, &accept_request),
        some_test_fail(),
        none_do_nothing()
    );
    IOHandle client = open_socket(2, IO_OPEN_READ | IO_OPEN_WRITE);
    IOReply accepted = execute(1, accept_request);
    TEST_ASSERT_EQUAL(IO_OK, accepted.status);
    IORequest read_request = IORequest__new(IO_REQUEST_READ, accepted.value);
    read_request.count = 10;
    match_optional(IOMediator__execute(&mediator, 1, 101, &read_request),
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(IOMediator__execute(&mediator, 1, 101, &read_request),
        some_test_fail(),
        none_do_nothing()
    );
    IORequest write_request = IORequest__new(IO_REQUEST_WRITE, client);
    write_request.count = 3;
    write_request.data[0] = 42;
    write_request.data[1] = 0;
    write_request.data[2] = 255;
    TEST_ASSERT_EQUAL_INT64(3, execute(2, write_request).value);
    IOReply read_reply = execute(1, read_request);
    TEST_ASSERT_EQUAL(IO_OK, read_reply.status);
    TEST_ASSERT_EQUAL_INT64(3, read_reply.value);
    TEST_ASSERT_EQUAL_MEMORY(write_request.data, read_reply.data, 3);
}

void test_handles_are_owned_and_protocol_semantics_are_checked(void)
{
    IOHandle listener = open_socket(1, IO_OPEN_LISTEN);
    IORequest forged = IORequest__new(IO_REQUEST_CLOSE, listener);
    forged.client_pid = 101;
    forged.client_session = 1;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(2, forged).status);
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, execute(1, IORequest__new(IO_REQUEST_READ, listener)).status);
    IOHandle client = open_socket(2, IO_OPEN_READ);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, execute(2, IORequest__new(IO_REQUEST_WRITE, client)).status);
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, execute(2, IORequest__new(IO_REQUEST_ACCEPT, client)).status);
    TEST_ASSERT_EQUAL(IO_OK, execute(2, IORequest__new(IO_REQUEST_CLOSE, client)).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(2, IORequest__new(IO_REQUEST_READ, client)).status);
    TEST_ASSERT_EQUAL(IO_OK, execute(1, IORequest__new(IO_REQUEST_CLOSE, listener)).status);
    TEST_ASSERT_EQUAL_UINT64(0, SocketResources__len(&sockets.resources));
    TEST_ASSERT_GREATER_THAN_INT64(listener, open_socket(1, IO_OPEN_LISTEN));
}

void test_disconnect_during_open_drains_reply_and_releases_unrecorded_resources(void)
{
    IORequest request = open_request(IO_OPEN_LISTEN);
    match_optional(IOMediator__execute(&mediator, 1, 101, &request),
        some_test_fail(),
        none_do_nothing()
    );
    bool done = false;
    for (unsigned retry = 0; retry < 20 && !done; retry++)
    {
        done = IOMediator__disconnect(&mediator, 1, 101);
    }
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_EQUAL_UINT64(0, SocketResources__len(&sockets.resources));
    TEST_ASSERT_EQUAL_UINT64(0, IOResources__len(&mediator.resources));
    TEST_ASSERT_EQUAL_UINT(2, requests);
    open_socket(2, IO_OPEN_LISTEN);
}

void test_peer_close_delivers_buffered_data_then_eof_and_other_handles_survive(void)
{
    IOHandle listener = open_socket(1, IO_OPEN_LISTEN);
    IOHandle client = open_socket(2, IO_OPEN_READ | IO_OPEN_WRITE);
    IOHandle peer = execute(1, IORequest__new(IO_REQUEST_ACCEPT, listener)).value;
    IORequest write_request = IORequest__new(IO_REQUEST_WRITE, client);
    write_request.count = 1;
    write_request.data[0] = 99;
    TEST_ASSERT_EQUAL(IO_OK, execute(2, write_request).status);
    TEST_ASSERT_EQUAL(IO_OK, execute(2, IORequest__new(IO_REQUEST_CLOSE, client)).status);
    IORequest read_request = IORequest__new(IO_REQUEST_READ, peer);
    read_request.count = 4;
    TEST_ASSERT_EQUAL_INT64(1, execute(1, read_request).value);
    TEST_ASSERT_EQUAL_INT64(0, execute(1, read_request).value);
    open_socket(3, IO_OPEN_READ | IO_OPEN_WRITE);
    TEST_ASSERT_EQUAL(IO_OK, execute(1, IORequest__new(IO_REQUEST_ACCEPT, listener)).status);
}

void test_backpressure_is_nonblocking_and_does_not_duplicate_bytes(void)
{
    IOHandle listener = open_socket(1, IO_OPEN_LISTEN);
    IOHandle client = open_socket(2, IO_OPEN_READ | IO_OPEN_WRITE);
    IOHandle peer = execute(1, IORequest__new(IO_REQUEST_ACCEPT, listener)).value;
    IORequest write_request = IORequest__new(IO_REQUEST_WRITE, client);
    write_request.count = IO_PROTOCOL_DATA_SIZE;
    for (unsigned i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_INT64(IO_PROTOCOL_DATA_SIZE, execute(2, write_request).value);
    }
    match_optional(IOMediator__execute(&mediator, 2, 102, &write_request),
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(IOMediator__execute(&mediator, 2, 102, &write_request),
        some_test_fail(),
        none_do_nothing()
    );
    IORequest read_request = IORequest__new(IO_REQUEST_READ, peer);
    read_request.count = 7;
    TEST_ASSERT_EQUAL_INT64(7, execute(1, read_request).value);
    TEST_ASSERT_EQUAL_INT64(7, execute(2, write_request).value);
}

void test_backend_failure_is_reported_without_reopening_or_replaying(void)
{
    IOHandle listener = open_socket(1, IO_OPEN_LISTEN);
    broken = true;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, execute(1, IORequest__new(IO_REQUEST_ACCEPT, listener)).status);
    unsigned before = requests;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, execute(2, open_request(IO_OPEN_READ)).status);
    TEST_ASSERT_EQUAL_UINT(before, requests);
}
