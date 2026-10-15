#include "unity.h"
#include "../channel_fixture.c"

void setUp(void)
{
    ChannelFixture__reset();
}

void tearDown(void)
{
}

void test_open_returns_a_distinct_direct_client_service_connection(void)
{
    Queue control = ChannelFixture__client();
    QueueConnectionID control_id = Queue__connection(&control);
    QueueMessage opened = ChannelFixture__open(&control);

    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_OPEN, opened.kind);
    TEST_ASSERT_NOT_EQUAL(0, opened.arg0);
    TEST_ASSERT_NOT_EQUAL(control_id, opened.arg0);

    Queue direct = unwrap_result(Queue__attach_existing(opened.arg0), ((Queue){0}));
    TEST_ASSERT_EQUAL_UINT64(8, Queue__peer_pid(&direct));
    TEST_ASSERT_EQUAL_UINT64(42, Queue__local_pid(&direct));
}

void test_channel_requests_bypass_the_warden_after_open(void)
{
    Channel channel = unwrap_result(Channel__open(&directory, &bstr("hundekanal-test-echo")), ((Channel){0}));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(0, ChannelSessions__len(&exchanges));

    uint8_t reply[8] = {0};
    match_result(
        Channel__submit(&channel, (ConstChannelDataPtr)"direct", 6, reply, sizeof(reply)),
        success(size)
        {
            TEST_ASSERT_EQUAL_UINT64(6, size);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_MEMORY("direct", reply, 6);
    TEST_ASSERT_EQUAL_UINT64(0, ChannelSessions__len(&exchanges));
}

void test_invalid_open_is_failed_without_creating_a_direct_connection(void)
{
    Queue control = ChannelFixture__client();
    QueueMessage invalid = ChannelControlMessage__new(CHANNEL_REQUEST_OPEN, 0, 0, Queue__connection(&control));
    ChannelFixture__push(&control, &invalid);

    QueueMessage response = ChannelFixture__receive(&control);
    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_FAILED, response.kind);
    TEST_ASSERT_EQUAL_UINT64(0, response.arg0);
}

void test_client_close_releases_the_temporary_control_session(void)
{
    Queue control = ChannelFixture__client();
    ChannelFixture__open(&control);
    TEST_ASSERT_EQUAL(QUEUE_OK, QueueConnection__close(Queue__connection(&control)));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(0, ChannelSessions__len(&exchanges));
}

void test_unconsumed_open_reply_rolls_back_the_delegated_connection(void)
{
    Queue control = ChannelFixture__client();
    QueueMessage open = ChannelControlMessage__new(CHANNEL_REQUEST_OPEN, echo_arrivals.endpoint, 0,
                                                   Queue__connection(&control));
    ChannelFixture__push(&control, &open);
    ChannelFixture__pump();
    ChannelFixture__pump();

    TEST_ASSERT_EQUAL(QUEUE_OK, QueueConnection__close(Queue__connection(&control)));
    ChannelFixture__drain();

    TEST_ASSERT_EQUAL_UINT64(1, cancelled_delegations);
    TEST_ASSERT_EQUAL_UINT64(0, ChannelSessions__len(&exchanges));
}
