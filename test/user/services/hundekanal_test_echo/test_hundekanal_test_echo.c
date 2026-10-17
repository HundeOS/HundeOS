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
    Channel channel = unwrap_result(Channel__open(&directory, &bstr("hundekanal-test-echo")), ((Channel){0}));
    uint8_t request[] = {'a', 0, 'z'};
    uint8_t reply[] = {0, 0, 0, 0x7f};

    match_result(
        Channel__submit(&channel, request, sizeof(request), reply, sizeof(reply)),
        success(size)
        {
            TEST_ASSERT_EQUAL_UINT64(sizeof(request), size);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_MEMORY(request, reply, sizeof(request));
    TEST_ASSERT_EQUAL_HEX8(0x7f, reply[3]);
}

void test_echo_connection_lookup_keeps_distinct_handles_after_removal(void)
{
    Channel first = unwrap_result(Channel__open(&directory, &bstr("hundekanal-test-echo")), ((Channel){0}));
    Channel second = unwrap_result(Channel__open(&directory, &bstr("hundekanal-test-echo")), ((Channel){0}));
    ChannelFixture__drain();

    TEST_ASSERT_EQUAL_UINT64(2, EchoConnections__len(&echoes));
    TEST_ASSERT_TRUE(EchoConnections__contains(&echoes, Queue__connection(&first.control)));
    TEST_ASSERT_TRUE(EchoConnections__contains(&echoes, Queue__connection(&second.control)));
    TEST_ASSERT_EQUAL(CHANNEL_OK, Channel__close(&first));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(1, EchoConnections__len(&echoes));
    TEST_ASSERT_EQUAL(CHANNEL_OK, Channel__close(&second));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(0, EchoConnections__len(&echoes));
}

void test_echo_rejects_connection_not_authorized_by_warden(void)
{
    Queue rogue = unwrap_result(Queue__connect(echo_arrivals.endpoint), ((Queue){0}));
    ChannelFixture__pump();
    TEST_ASSERT_EQUAL(QUEUE_CLOSED, Queue__status(&rogue));
    TEST_ASSERT_EQUAL_UINT64(0, EchoConnections__len(&echoes));
}

void test_echo_rejects_oversized_direct_request(void)
{
    Queue control = ChannelFixture__client();
    QueueMessage opened = ChannelFixture__open(&control);
    Queue direct = unwrap_result(Queue__attach_existing(opened.arg0), ((Queue){0}));
    TEST_ASSERT_EQUAL(QUEUE_OK, QueueConnection__close(Queue__connection(&control)));
    Queue__wait_writable(&direct);

    QueueMessage request =
        ChannelControlMessage__new(CHANNEL_REQUEST_SUBMIT, 0, CHANNEL_PAYLOAD_CAPACITY, Queue__connection(&direct));
    ChannelFixture__push(&direct, &request);
    QueueMessage reply = ChannelFixture__receive(&direct);
    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_FAILED, reply.kind);
}

void test_echo_idle_scan_reports_no_work(void)
{
    TEST_ASSERT_FALSE(EchoConnections__step(&echoes, &echo_arrivals, 7));
}
