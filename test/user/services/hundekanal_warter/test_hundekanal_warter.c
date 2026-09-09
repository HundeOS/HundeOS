#include "../channel_fixture.c"

void setUp(void)
{
    ChannelFixture__reset();
}
void tearDown(void)
{
}

void test_exchange_advances_through_states_and_preserves_idle_states(void)
{
    AtomicMessageQueue client = ChannelFixture__client();
    run_echo = false;
    match_optional(
        ChannelExchanges__ptr_at(&exchanges, 0),
        some(exchange)
        {
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_WAIT_OPEN, ChannelExchange__state(exchange));
            ServiceQueueMessage open = ChannelExchange__message(CHANNEL_REQUEST_OPEN, echo_arrivals.endpoint, 0, 0);
            ChannelFixture__push(&client, &open);
            ChannelFixture__pump();
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_CONNECTING, ChannelExchange__state(exchange));
            ChannelFixture__pump();
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_CONNECTING, ChannelExchange__state(exchange));
            TEST_ASSERT_EQUAL_UINT64(0, allocated_pages);

            run_echo = true;
            ChannelFixture__pump();
            ChannelFixture__pump();
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_DELIVERING_OPEN, ChannelExchange__state(exchange));
            ChannelFixture__pump();
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_OPEN, ChannelExchange__state(exchange));
            TEST_ASSERT_EQUAL_UINT64(1, allocated_pages);
            TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_OPEN, ChannelFixture__receive(&client).kind);

            run_echo = false;
            ServiceQueueMessage submit = ChannelExchange__message(CHANNEL_REQUEST_SUBMIT, 0, 0, client.connection);
            ChannelFixture__push(&client, &submit);
            ChannelFixture__pump();
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_SUBMITTING, ChannelExchange__state(exchange));
            ChannelFixture__pump();
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_WORKING, ChannelExchange__state(exchange));
            current_pid = 7;
            TEST_ASSERT_FALSE(ChannelExchange__step(exchange));
            current_pid = 42;
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_WORKING, ChannelExchange__state(exchange));

            run_echo = true;
            ChannelFixture__pump();
            ChannelFixture__pump();
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_WORKING, ChannelExchange__state(exchange));
            ChannelFixture__pump();
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_CLOSING_SERVER, ChannelExchange__state(exchange));
            match_optional(exchange->server,
                some(server){},
                none_test_fail()
            );
            ChannelFixture__pump();
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_DELIVERING_REPLY, ChannelExchange__state(exchange));
            match_optional(exchange->server,
                some_test_fail(),
                none_do_nothing()
            );
            TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_REPLIED, ChannelFixture__receive(&client).kind);
            TEST_ASSERT_EQUAL_UINT64(0, released_pages);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_OK, atomic_message_queue_close(client.connection));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(1, released_pages);
    TEST_ASSERT_EQUAL_UINT64(0, ChannelExchanges__len(&exchanges));
}

void test_submission_checks_already_queued_completion_in_the_same_step(void)
{
    AtomicMessageQueue client = ChannelFixture__client();
    ChannelFixture__open(&client);
    run_echo = false;
    ServiceQueueMessage submit = ChannelExchange__message(CHANNEL_REQUEST_SUBMIT, 0, 0, client.connection);
    ChannelFixture__push(&client, &submit);
    ChannelFixture__pump();
    match_optional(
        EchoConnections__ptr_at(&echoes, 0),
        some(echo)
        {
            ServiceQueueMessage reply = ChannelExchange__message(CHANNEL_RESPONSE_REPLIED, 0, 0, client.connection);
            ChannelFixture__push(&echo->queue, &reply);
        },
        none_test_fail()
    );
    ChannelFixture__pump();
    match_optional(
        ChannelExchanges__ptr_at(&exchanges, 0),
        some(exchange)
        {
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_CLOSING_SERVER, ChannelExchange__state(exchange));
            match_optional(exchange->server,
                some(server){},
                none_test_fail()
            );
            ChannelFixture__pump();
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_DELIVERING_REPLY, ChannelExchange__state(exchange));
            match_optional(exchange->server,
                some_test_fail(),
                none_do_nothing()
            );
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_REPLIED, ChannelFixture__receive(&client).kind);
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_OK, atomic_message_queue_close(client.connection));
    run_echo = true;
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(1, released_pages);
}

void test_full_server_queue_keeps_submission_pending_until_retry(void)
{
    AtomicMessageQueue client = ChannelFixture__client();
    ChannelFixture__open(&client);
    run_echo = false;
    match_optional(
        ChannelExchanges__ptr_at(&exchanges, 0),
        some(exchange)
        {
            match_optional(
                exchange->server,
                some(server)
                {
                    ServiceQueueMessage filler = ChannelExchange__message(99, 0, 0, 0);
                    for (size_t index = 0; index < ATOMIC_MESSAGE_QUEUE_CAPACITY; index++)
                    {
                        ChannelFixture__push(&server, &filler);
                    }
                    exchange->server = Some(AtomicMessageQueue, server);
                },
                none_test_fail()
            );
            ServiceQueueMessage submit = ChannelExchange__message(CHANNEL_REQUEST_SUBMIT, 0, 0, client.connection);
            ChannelFixture__push(&client, &submit);
            ChannelFixture__pump();
            current_pid = 7;
            TEST_ASSERT_FALSE(ChannelExchange__step(exchange));
            current_pid = 42;
            TEST_ASSERT_EQUAL_PTR(&CHANNEL_EXCHANGE_SUBMITTING, ChannelExchange__state(exchange));
        },
        none_test_fail()
    );
    match_optional(
        EchoConnections__ptr_at(&echoes, 0),
        some(echo)
        {
            for (size_t index = 0; index < ATOMIC_MESSAGE_QUEUE_CAPACITY; index++)
            {
                match_result(
                    AtomicMessageQueue__pop(&echo->queue),
                    success(message) { TEST_ASSERT_EQUAL_UINT64(99, message.kind); },
                    error_test_fail()
                );
            }
        },
        none_test_fail()
    );
    run_echo = true;
    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_REPLIED, ChannelFixture__receive(&client).kind);
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_OK, atomic_message_queue_close(client.connection));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(1, released_pages);
    TEST_ASSERT_EQUAL_UINT64(0, ChannelExchanges__len(&exchanges));
}

void test_connection_lookup_preserves_distinct_clients_and_removal(void)
{
    TEST_ASSERT_FALSE(ChannelExchanges__contains(&exchanges, 1));
    AtomicMessageQueue first = ChannelFixture__client();
    AtomicMessageQueue second = ChannelFixture__client();
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(2, ChannelExchanges__len(&exchanges));
    TEST_ASSERT_TRUE(ChannelExchanges__contains(&exchanges, first.connection));
    TEST_ASSERT_TRUE(ChannelExchanges__contains(&exchanges, second.connection));
    TEST_ASSERT_FALSE(ChannelExchanges__contains(&exchanges, UINT64_MAX));
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_OK, atomic_message_queue_close(first.connection));
    ChannelFixture__drain();
    TEST_ASSERT_FALSE(ChannelExchanges__contains(&exchanges, first.connection));
    TEST_ASSERT_TRUE(ChannelExchanges__contains(&exchanges, second.connection));
    TEST_ASSERT_EQUAL_UINT64(1, ChannelExchanges__len(&exchanges));
    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_OPEN, ChannelFixture__open(&second).kind);
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_OK, atomic_message_queue_close(second.connection));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(0, ChannelExchanges__len(&exchanges));
}

void test_protected_echo_exchange_releases_payload_after_client_close(void)
{
    TEST_ASSERT_EQUAL(0, ChannelEchoClient__run(&directory, &bstr("hello protected channel")));
    TEST_ASSERT_EQUAL_STRING("hello protected channel\n", output);
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(1, allocated_pages);
    TEST_ASSERT_EQUAL_UINT64(1, released_pages);
    TEST_ASSERT_EQUAL_UINT64(0, ChannelExchanges__len(&exchanges));
    TEST_ASSERT_EQUAL_UINT64(0, EchoConnections__len(&echoes));
}

void test_repeated_exchanges_reuse_slots_without_stale_state(void)
{
    for (size_t round = 0; round < 64; round++)
    {
        TEST_ASSERT_EQUAL(0, ChannelEchoClient__run(&directory, &bstr("echo")));
        ChannelFixture__drain();
    }
    TEST_ASSERT_EQUAL_UINT64(64, allocated_pages);
    TEST_ASSERT_EQUAL_UINT64(64, released_pages);
}

void test_forged_sender_field_cannot_change_payload_participants(void)
{
    AtomicMessageQueue client = ChannelFixture__client();
    ServiceQueueMessage opened = ChannelFixture__open(&client);
    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_OPEN, opened.kind);
    TEST_ASSERT_EQUAL_UINT64(42, last_allocated_client);
    atomic_message_queue_close(client.connection);
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(1, released_pages);
}

void test_kernel_looking_message_does_not_act_as_an_exit_event(void)
{
    AtomicMessageQueue client = ChannelFixture__client();
    ServiceQueueMessage fake = ServiceQueueMessage__process_exit(8, 0);
    ChannelFixture__push(&client, &fake);
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_CLOSED, AtomicMessageQueue__status(&client));
    TEST_ASSERT_EQUAL_UINT64(0, allocated_pages);
    atomic_message_queue_close(client.connection);
    TEST_ASSERT_EQUAL(0, ChannelEchoClient__run(&directory, &bstr("still alive")));
}

void test_stale_submit_token_cannot_dispatch_work(void)
{
    AtomicMessageQueue client = ChannelFixture__client();
    ChannelFixture__open(&client);
    ServiceQueueMessage submit = ChannelExchange__message(CHANNEL_REQUEST_SUBMIT, 0, 1, client.connection - 1);
    ChannelFixture__push(&client, &submit);
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_CLOSED, AtomicMessageQueue__status(&client));
    TEST_ASSERT_EQUAL_UINT64(1, released_pages);
    atomic_message_queue_close(client.connection);
}

void test_allocation_failure_returns_failure_and_cleans_connections(void)
{
    fail_allocation = true;
    TEST_ASSERT_EQUAL(1, ChannelEchoClient__run(&directory, &bstr("fail")));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(0, allocated_pages);
    TEST_ASSERT_EQUAL_UINT64(0, ChannelExchanges__len(&exchanges));
    TEST_ASSERT_EQUAL_UINT64(0, EchoConnections__len(&echoes));
}

void test_release_failure_retains_private_record_for_retry(void)
{
    fail_release = true;
    TEST_ASSERT_EQUAL(0, ChannelEchoClient__run(&directory, &bstr("retry")));
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(0, released_pages);
    TEST_ASSERT_EQUAL_UINT64(1, ChannelExchanges__len(&exchanges));
    fail_release = false;
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(1, released_pages);
    TEST_ASSERT_EQUAL_UINT64(0, ChannelExchanges__len(&exchanges));
}

void test_client_disconnect_before_server_accept_does_not_allocate(void)
{
    run_echo = false;
    AtomicMessageQueue client = ChannelFixture__client();
    ServiceQueueMessage open = ChannelExchange__message(CHANNEL_REQUEST_OPEN, echo_arrivals.endpoint, 0, 0);
    ChannelFixture__push(&client, &open);
    ChannelFixture__pump();
    atomic_message_queue_close(client.connection);
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, allocated_pages, "Pending connection must not allocate a payload");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, ChannelExchanges__len(&exchanges), "Pending exchange must be removed");
    run_echo = true;
    ChannelFixture__drain();
}

void test_client_disconnect_during_server_work_waits_for_completion(void)
{
    AtomicMessageQueue client = ChannelFixture__client();
    ChannelFixture__open(&client);
    run_echo = false;
    ServiceQueueMessage submit = ChannelExchange__message(CHANNEL_REQUEST_SUBMIT, 0, 1, client.connection);
    ChannelFixture__push(&client, &submit);
    ChannelFixture__pump();
    ChannelFixture__pump();
    atomic_message_queue_close(client.connection);
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(0, released_pages);
    run_echo = true;
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(1, released_pages);
}

void test_server_disconnect_invalidates_client_but_waits_for_its_close(void)
{
    AtomicMessageQueue client = ChannelFixture__client();
    ChannelFixture__open(&client);
    current_pid = 8;
    for_each_atomic_fa_ptr(AtomicMessageQueueConnectionSlots, &echo_arrivals.connections, index, id, {
        if (id != 0)
        {
            atomic_message_queue_close(id);
        }
    });
    current_pid = 42;
    ServiceQueueMessage failed = ChannelFixture__receive(&client);
    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_FAILED, failed.kind);
    TEST_ASSERT_EQUAL_UINT64(0, released_pages);
    atomic_message_queue_close(client.connection);
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(1, released_pages);
}

void test_corrupt_client_queue_does_not_prevent_another_exchange(void)
{
    AtomicMessageQueue rogue = ChannelFixture__client();
    rogue.outgoing->produced = UINT64_MAX;
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_CLOSED, AtomicMessageQueue__status(&rogue));
    atomic_message_queue_close(rogue.connection);
    TEST_ASSERT_EQUAL(0, ChannelEchoClient__run(&directory, &bstr("isolated")));
}

void test_stale_server_endpoint_is_rejected_after_restart(void)
{
    uint64_t old_endpoint = echo_arrivals.endpoint;
    echo_arrivals.endpoint += 16;
    AtomicMessageQueue client = ChannelFixture__client();
    TEST_ASSERT_EQUAL(1, ChannelEchoClient__exchange(&client, old_endpoint, &bstr("old")));
    atomic_message_queue_close(client.connection);
    ChannelFixture__drain();
    directory.entries[1].generation = echo_arrivals.endpoint;
    TEST_ASSERT_EQUAL(0, ChannelEchoClient__run(&directory, &bstr("new")));
}

void test_full_reply_queue_retains_open_response_without_blocking_other_clients(void)
{
    AtomicMessageQueue slow = ChannelFixture__client();
    ServiceQueueMessage filler = ChannelExchange__message(99, 0, 0, 0);
    match_optional(
        ChannelExchanges__ptr_at(&exchanges, 0),
        some(exchange)
        {
            for (size_t index = 0; index < ATOMIC_MESSAGE_QUEUE_CAPACITY; index++)
            {
                ChannelFixture__push(&exchange->client, &filler);
            }
        },
        none_test_fail()
    );
    ServiceQueueMessage open =
        ChannelExchange__message(CHANNEL_REQUEST_OPEN, echo_arrivals.endpoint, 0, slow.connection);
    ChannelFixture__push(&slow, &open);
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(1, allocated_pages);
    current_pid = 43;
    TEST_ASSERT_EQUAL(0, ChannelEchoClient__run(&directory, &bstr("not blocked")));
    ChannelFixture__drain();
    current_pid = 42;
    for (size_t index = 0; index < ATOMIC_MESSAGE_QUEUE_CAPACITY; index++)
    {
        match_result(
            AtomicMessageQueue__pop(&slow),
            success(message) { TEST_ASSERT_EQUAL_UINT64(99, message.kind); },
            error_test_fail()
        );
    }
    ServiceQueueMessage reply = ChannelFixture__receive(&slow);
    TEST_ASSERT_EQUAL(CHANNEL_RESPONSE_OPEN, reply.kind);
    atomic_message_queue_close(slow.connection);
    ChannelFixture__drain();
    TEST_ASSERT_EQUAL_UINT64(allocated_pages, released_pages);
}
