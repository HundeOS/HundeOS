#include "unity.h"
#include "ipc/channel.h"

static QueueIdentity identity;
static QueuePage client_page;
static QueuePage server_page;
static ChannelPayload payload;
static Channel channel;
static Queue server;

void setUp(void)
{
    identity = (QueueIdentity){.connection = 17, .client_pid = 4, .server_pid = 5, .origin_pid = 4, .accepted = 1};
    client_page = (QueuePage){0};
    server_page = (QueuePage){0};
    payload = (ChannelPayload){0};
    Queue client = Queue__new(&identity, &client_page, &server_page, 17, 5);
    server = Queue__new(&identity, &server_page, &client_page, 17, 4);
    channel = Channel__new(&client, &payload);
}

void tearDown(void)
{
}

static void request(void)
{
    match_result(
        Channel__try_request(&channel, (ConstChannelDataPtr) "request", 7),
        success(code)
        {
            TEST_ASSERT_EQUAL(CHANNEL_OK, code);
        },
        error_test_fail()
    );
}

static void respond(QueueConnectionID token)
{
    TEST_ASSERT_TRUE(ChannelPayload__write_reply(&payload, (ConstChannelDataPtr) "reply", 5));
    QueueMessage response = ChannelControlMessage__new(CHANNEL_RESPONSE_REPLIED, 0, 5, token);
    response.request = channel.ticket.metadata;
    match_result(Queue__push(&server, &response),
        success_do_nothing(),
        error_test_fail()
    );
}

void test_request_and_reply_share_the_existing_queue_transport(void)
{
    request();
    match_result(
        Queue__pop(&server),
        success(message)
        {
            TEST_ASSERT_EQUAL(CHANNEL_REQUEST_SUBMIT, message.kind);
            TEST_ASSERT_EQUAL_UINT64(7, message.arg1);
            TEST_ASSERT_EQUAL_UINT64(17, message.arg2);
        },
        error_test_fail()
    );
    respond(17);
    uint8_t reply[8];
    match_result(
        Channel__try_poll_reply(&channel, reply, sizeof(reply)),
        success(size)
        {
            match_optional(size,
            some(count)
            {
                TEST_ASSERT_EQUAL_UINT64(5, count);
            },
            none_test_fail()
        );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_MEMORY("reply", reply, 5);
    match_result(
        Queue__pop(&server),
        success(message)
        {
            TEST_ASSERT_EQUAL(CHANNEL_REQUEST_ACKNOWLEDGE, message.kind);
        },
        error_test_fail()
    );
    request();
}

void test_explicit_origin_is_stored_in_request_ticket(void)
{
    channel = Channel__new_with_origin(&channel.control, &payload, 41);

    request();

    match_result(
        Queue__pop(&server),
        success(message)
        {
            TEST_ASSERT_EQUAL_UINT64(41, message.request.origin_pid);
        },
        error_test_fail()
    );
}

void test_full_request_queue_returns_pending_without_starting_request(void)
{
    QueueMessage filler = QueueMessage__new(0, 99);
    for (size_t index = 0; index < QUEUE_CAPACITY; index++)
    {
        match_result(Queue__push(&channel.control, &filler),
            success_do_nothing(),
            error_test_fail()
        );
    }
    match_result(
        Channel__try_request(&channel, (ConstChannelDataPtr) "request", 7),
        success(code)
        {
            TEST_ASSERT_EQUAL(CHANNEL_PENDING, code);
        },
        error_test_fail()
    );
    match_result(Queue__pop(&server),
        success_do_nothing(),
        error_test_fail()
    );
    request();
}

void test_ack_backpressure_retains_reply_and_prevents_a_second_request(void)
{
    request();
    QueueMessage filler = QueueMessage__new(0, 99);
    for (size_t index = 1; index < QUEUE_CAPACITY; index++)
    {
        match_result(Queue__push(&channel.control, &filler),
            success_do_nothing(),
            error_test_fail()
        );
    }
    respond(17);
    uint8_t reply[8];
    for (size_t attempt = 0; attempt < 2; attempt++)
    {
        match_result(
            Channel__try_poll_reply(&channel, reply, sizeof(reply)),
            success(size)
            {
                match_optional(size,
                some_test_fail(),
                none_do_nothing()
            );
            },
            error_test_fail()
        );
    }
    match_result(
        Channel__try_request(&channel, (ConstChannelDataPtr) "next", 4),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(CHANNEL_BUSY, code);
        }
    );
    match_result(Queue__pop(&server),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(
        Channel__try_poll_reply(&channel, reply, sizeof(reply)),
        success(size)
        {
            match_optional(size,
            some(count)
            {
                TEST_ASSERT_EQUAL_UINT64(5, count);
            },
            none_test_fail()
        );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_MEMORY("reply", reply, 5);
}

void test_empty_poll_is_pending_and_closed_transport_fails(void)
{
    request();
    uint8_t reply[8];
    match_result(
        Channel__try_poll_reply(&channel, reply, sizeof(reply)),
        success(size)
        {
            match_optional(size,
            some_test_fail(),
            none_do_nothing()
        );
        },
        error_test_fail()
    );
    identity.closed = 1;
    match_result(
        Channel__try_poll_reply(&channel, reply, sizeof(reply)),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_CLOSED, code);
        }
    );
}

void test_reply_token_is_validated_before_acknowledgement(void)
{
    request();
    respond(18);
    uint8_t reply[8];
    match_result(
        Channel__try_poll_reply(&channel, reply, sizeof(reply)),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(CHANNEL_INVALID_REPLY, code);
        }
    );
}

void test_small_reply_buffer_can_be_retried_without_losing_response(void)
{
    request();
    respond(17);
    uint8_t reply[8];
    match_result(
        Channel__try_poll_reply(&channel, reply, 2),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(CHANNEL_INVALID_REPLY, code);
        }
    );
    match_result(
        Channel__try_poll_reply(&channel, reply, sizeof(reply)),
        success(size)
        {
            match_optional(size,
            some(count)
            {
                TEST_ASSERT_EQUAL_UINT64(5, count);
            },
            none_test_fail()
        );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_MEMORY("reply", reply, 5);
}

void test_request_ticket_increments_and_rejects_previous_reply(void)
{
    request();
    RequestTicket first = channel.ticket;
    respond(17);
    uint8_t reply[8];
    match_result(Channel__try_poll_reply(&channel, reply, sizeof(reply)),
        success_do_nothing(),
        error_test_fail()
    );
    request();
    TEST_ASSERT_EQUAL_UINT64(first.metadata.generation + 1, channel.ticket.metadata.generation);
    QueueMessage stale = ChannelControlMessage__new(CHANNEL_RESPONSE_REPLIED, 0, 5, 17);
    stale.request = first.metadata;
    match_result(Queue__push(&server, &stale),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(
        Channel__try_poll_reply(&channel, reply, sizeof(reply)),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(CHANNEL_INVALID_REPLY, code);
        }
    );
    TEST_ASSERT_TRUE(channel.request_pending);
}

void test_nested_ticket_preserves_origin_and_immediate_parent(void)
{
    RequestTicket parent = RequestTicket__new(91, 12, 77, &None(RequestTicket));
    match_result(
        Channel__try_request_with_parent(&channel, (ConstChannelDataPtr) "request", 7, &Some(RequestTicket, parent)),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(17, channel.ticket.connection);
    TEST_ASSERT_EQUAL_UINT64(1, channel.ticket.metadata.generation);
    TEST_ASSERT_EQUAL_UINT64(77, channel.ticket.metadata.origin_pid);
    TEST_ASSERT_EQUAL_UINT64(91, channel.ticket.metadata.parent_connection);
    TEST_ASSERT_EQUAL_UINT64(12, channel.ticket.metadata.parent_generation);
    match_result(
        QueuePublications__at(&client_page.publication, 0),
        success(publication)
        {
            RequestTicket observed = ChannelControlMessage__ticket(&publication.message);
            TEST_ASSERT_TRUE(RequestTicket__equals(&channel.ticket, &observed));
            TEST_ASSERT_EQUAL_UINT64(client_page.produced, publication.cursor);
        },
        error_test_fail()
    );
}

void test_ticket_generation_does_not_advance_on_backpressure_or_wrap(void)
{
    channel.ticket = RequestTicket__new(17, UINT64_MAX, 4, &None(RequestTicket));
    match_result(
        Channel__try_request(&channel, (ConstChannelDataPtr) "x", 1),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(CHANNEL_INVALID_REPLY, code);
        }
    );
    TEST_ASSERT_EQUAL_UINT64(0, client_page.produced);
}
