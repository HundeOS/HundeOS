#include "../channel_fixture.c"

void setUp(void)
{
    ChannelFixture__reset();
}

void tearDown(void)
{
}

void test_echo_copies_binary_payload_without_writing_past_length(void)
{
    Queue client = ChannelFixture__client();
    QueueMessage opened = ChannelFixture__open(&client);
    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_OPEN, opened.kind);
    ChannelPayloadPtr payload = (ChannelPayloadPtr)opened.arg0;
    payload->request[0] = 'a';
    payload->request[1] = 0;
    payload->request[2] = 'z';
    payload->reply[3] = 0x7f;
    QueueMessage submit = ChannelControlMessage__new(CHANNEL_REQUEST_SUBMIT, 0, 3, client.connection);
    submit.request = (RequestMetadata){.generation = 1, .origin_pid = 42};
    ChannelFixture__push(&client, &submit);
    QueueMessage reply = ChannelFixture__receive(&client);
    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_REPLIED, reply.kind);
    TEST_ASSERT_EQUAL_UINT64(3, reply.arg1);
    TEST_ASSERT_EQUAL_MEMORY(payload->request, payload->reply, 3);
    TEST_ASSERT_EQUAL_HEX8(0x7f, payload->reply[3]);
    TEST_ASSERT_EQUAL(QUEUE_OK, QueueConnection__close(client.connection));
    ChannelFixture__drain();
}

void test_echo_connection_lookup_keeps_distinct_handles_after_removal(void)
{
    TEST_ASSERT_FALSE(EchoConnections__contains(&echoes, 1));
    current_pid = 7;
    Result(Queue) first_result = Queue__connect(echo_arrivals.endpoint);
    Queue first = unwrap_result(first_result, (Queue){0});
    Result(Queue) second_result = Queue__connect(echo_arrivals.endpoint);
    Queue second = unwrap_result(second_result, (Queue){0});
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(2, EchoConnections__len(&echoes));
    TEST_ASSERT_TRUE(EchoConnections__contains(&echoes, first.connection));
    TEST_ASSERT_TRUE(EchoConnections__contains(&echoes, second.connection));
    TEST_ASSERT_FALSE(EchoConnections__contains(&echoes, UINT64_MAX));
    TEST_ASSERT_EQUAL(QUEUE_OK, QueueConnection__close(first.connection));
    ChannelFixture__drain();
    TEST_ASSERT_FALSE(EchoConnections__contains(&echoes, first.connection));
    TEST_ASSERT_TRUE(EchoConnections__contains(&echoes, second.connection));
    TEST_ASSERT_EQUAL_UINT64(1, EchoConnections__len(&echoes));
    TEST_ASSERT_EQUAL(QUEUE_OK, QueueConnection__close(second.connection));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(0, EchoConnections__len(&echoes));
}

void test_echo_rejects_direct_non_warden_connection(void)
{
    Result(Queue) connected = Queue__connect(echo_arrivals.endpoint);
    Queue rogue = unwrap_result(connected, (Queue){0});
    ChannelFixture__pump();
    TEST_ASSERT_EQUAL(QUEUE_CLOSED, Queue__status(&rogue));
    TEST_ASSERT_EQUAL_UINT64(0, EchoConnections__len(&echoes));
}

void test_echo_rejects_oversized_request_without_touching_payload(void)
{
    current_pid = 7;
    Result(Queue) connected = Queue__connect(echo_arrivals.endpoint);
    Queue warden = unwrap_result(connected, (Queue){0});
    ChannelFixture__pump();
    QueueMessage request = ChannelControlMessage__new(CHANNEL_REQUEST_SERVE, 1, CHANNEL_PAYLOAD_CAPACITY, 123);
    ChannelFixture__push(&warden, &request);
    QueueMessage reply = ChannelFixture__receive(&warden);
    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_FAILED, reply.kind);
    TEST_ASSERT_EQUAL_UINT64(123, reply.arg2);
}

void test_echo_idle_scan_reports_no_work(void)
{
    TEST_ASSERT_FALSE(EchoConnections__step(&echoes, &echo_arrivals, 7));
}

void test_echo_rejects_connection_when_warden_is_not_registered(void)
{
    current_pid = 7;
    Result(Queue) connected = Queue__connect(echo_arrivals.endpoint);
    Queue warden = unwrap_result(connected, (Queue){0});
    current_pid = 8;
    TEST_ASSERT_TRUE(EchoConnections__step(&echoes, &echo_arrivals, 0));
    TEST_ASSERT_EQUAL(QUEUE_CLOSED, Queue__status(&warden));
}
