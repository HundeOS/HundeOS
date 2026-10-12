#include "unity.h"
#include "ipc/queue.h"

static QueueIdentity identity;
static QueuePage client_page;
static QueuePage server_page;
static IpcDoorbell client_doorbell;
static IpcDoorbell server_doorbell;
static QueueConnectionID recorded_connection;
static uint64_t notification_count;

static void record_data_available(QueueConnectionID connection)
{
    recorded_connection = connection;
    notification_count++;
}

void setUp(void)
{
    identity = (QueueIdentity){.connection = 17, .client_pid = 4, .server_pid = 5, .origin_pid = 4, .accepted = 1};
    client_page = (QueuePage){0};
    server_page = (QueuePage){0};
    client_doorbell = (IpcDoorbell){0};
    server_doorbell = (IpcDoorbell){0};
    recorded_connection = 0;
    notification_count = 0;
}

void test_push_notifies_after_publication_and_pop_does_not_notify(void)
{
    identity.accepted = 1;
    Queue client = Queue__new(&identity, &client_page, &server_page, 17, 5, &server_doorbell, &record_data_available);
    Queue server = Queue__new(&identity, &server_page, &client_page, 17, 4, &client_doorbell, &record_data_available);
    QueueMessage message = {0};

    match_result(Queue__push(&client, &message),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(17, recorded_connection);
    TEST_ASSERT_EQUAL_UINT64(1, notification_count);
    match_result(Queue__pop(&server),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(1, IpcEvent__generation(IpcDoorbell__event(&client_doorbell)));
    TEST_ASSERT_EQUAL_UINT64(1, notification_count);
}

void tearDown(void)
{
}

static Queue client_queue(void)
{
    match_result(
        Queue__attach(&identity, &client_page, &server_page, 17, 4, &server_doorbell, &Queue__ignore_data_available),
        success(queue)
        {
            return queue;
        },
        error_test_fail()
    );
    return (Queue){0};
}

void test_new_constructs_a_raw_queue_with_zeroed_private_cursors(void)
{
    Queue queue =
        Queue__new(&identity, &server_page, &client_page, 23, 7, &client_doorbell, &Queue__ignore_data_available);
    TEST_ASSERT_EQUAL_PTR(&identity, queue.identity);
    TEST_ASSERT_EQUAL_PTR(&server_page, queue.outgoing);
    TEST_ASSERT_EQUAL_PTR(&client_page, queue.incoming);
    TEST_ASSERT_EQUAL_UINT64(23, queue.connection);
    TEST_ASSERT_EQUAL_UINT64(7, queue.peer_pid);
    TEST_ASSERT_EQUAL_PTR(&client_doorbell, queue.peer_doorbell);
    TEST_ASSERT_EQUAL_UINT64(0, queue.read);
    TEST_ASSERT_EQUAL_UINT64(0, queue.written);
    TEST_ASSERT_EQUAL_UINT64(0, queue.acknowledged);
}

void test_attach_rejects_invalid_connection_and_participant(void)
{
    match_result(
        Queue__attach(&identity, &client_page, &server_page, 18, 4, &server_doorbell, &Queue__ignore_data_available),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
    match_result(
        Queue__attach(&identity, &client_page, &server_page, 17, 6, &server_doorbell, &Queue__ignore_data_available),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
}

void test_origin_pid_comes_from_authenticated_identity(void)
{
    Queue queue = client_queue();
    TEST_ASSERT_EQUAL_UINT64(4, Queue__origin_pid(&queue));
    identity.origin_pid = 41;
    TEST_ASSERT_EQUAL_UINT64(41, Queue__origin_pid(&queue));
}

void test_push_returns_success_result_and_pop_authenticates_sender(void)
{
    Queue client = client_queue();
    QueueMessage sent = QueueMessage__new(999, 7);
    IpcEventGeneration server_generation = IpcEvent__generation(IpcDoorbell__event(&server_doorbell));
    match_result(
        Queue__push(&client, &sent),
        success(status)
        {
            TEST_ASSERT_EQUAL(QUEUE_OK, status);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(server_generation + 1, IpcEvent__generation(IpcDoorbell__event(&server_doorbell)));
    match_result(
        Queue__attach(&identity, &client_page, &server_page, 17, 5, &client_doorbell, &Queue__ignore_data_available),
        success(server)
        {
            IpcEventGeneration client_generation = IpcEvent__generation(IpcDoorbell__event(&client_doorbell));
            match_result(
                Queue__pop(&server),
                success(received)
                {
                    TEST_ASSERT_EQUAL_UINT64(4, received.sender_pid);
                    TEST_ASSERT_EQUAL_UINT64(7, received.kind);
                },
                error_test_fail()
            );
            TEST_ASSERT_EQUAL_UINT64(client_generation + 1, IpcEvent__generation(IpcDoorbell__event(&client_doorbell)));
        },
        error_test_fail()
    );
}

void test_push_reports_pending_full_and_corrupt_cursors_as_errors(void)
{
    Queue queue = client_queue();
    QueueMessage message = QueueMessage__new(0, 1);
    identity.accepted = 0;
    match_result(
        Queue__push(&queue, &message),
        success_test_fail(),
        error(code, error_message)
        {
            TEST_ASSERT_EQUAL(QUEUE_PENDING, code);
        }
    );
    identity.accepted = 1;
    for (uint64_t index = 0; index < QUEUE_CAPACITY; index++)
    {
        match_result(Queue__push(&queue, &message),
            success_do_nothing(),
            error_test_fail()
        );
    }
    match_result(
        Queue__push(&queue, &message),
        success_test_fail(),
        error(code, error_message)
        {
            TEST_ASSERT_EQUAL(QUEUE_FULL, code);
        }
    );
    server_page.consumed = UINT64_MAX;
    match_result(
        Queue__push(&queue, &message),
        success_test_fail(),
        error(code, error_message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
}

void test_write_capacity_requires_acceptance_and_releases_closed_waiters(void)
{
    Queue queue = client_queue();

    TEST_ASSERT_TRUE(Queue__has_write_capacity(&queue));
    identity.accepted = 0;
    TEST_ASSERT_FALSE(Queue__has_write_capacity(&queue));
    identity.closed = 1;
    TEST_ASSERT_TRUE(Queue__has_write_capacity(&queue));
}

void test_layout_and_arrival_access_have_explicit_bounds(void)
{
    QueueArrivals arrivals = {0};
    TEST_ASSERT_EQUAL(1024, QUEUE_LIMIT);
    TEST_ASSERT_EQUAL(32, QUEUE_CAPACITY);
    TEST_ASSERT_EQUAL_UINT64(3 * QUEUE_PAGE_SIZE, QUEUE_ARRIVALS_SIZE);
    match_result(QueueConnectionSlots__set(&arrivals.connections, QUEUE_LIMIT - 1, 2047),
        success_do_nothing(),
                 error_test_fail()
    );
    match_optional(
        QueueArrivals__connection(&arrivals, QUEUE_LIMIT - 1),
        some(connection)
        {
            TEST_ASSERT_EQUAL_UINT64(2047, connection);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL_HEX64(QUEUE_CONNECTION_BASE + (QUEUE_LIMIT - 1) * QUEUE_CONNECTION_SIZE,
                            QueueLayout__address(2047));
    match_result(QueueConnectionSlots__set(&arrivals.connections, 3, 67),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_HEX64(QUEUE_CONNECTION_BASE + QUEUE_CONNECTION_SIZE, QueueLayout__address(QUEUE_LIMIT + 1));
    match_optional(
        QueueArrivals__connection(&arrivals, 3),
        some(connection)
        {
            TEST_ASSERT_EQUAL_UINT64(67, connection);
        },
        none_test_fail()
    );
    match_optional(QueueArrivals__connection(&arrivals, QUEUE_LIMIT),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_unread_query_uses_shared_acknowledgement_and_bounds(void)
{
    Queue queue = client_queue();
    TEST_ASSERT_FALSE(Queue__has_unread_messages(&queue));
    server_page.produced = 7;
    client_page.consumed = 6;
    TEST_ASSERT_TRUE(Queue__has_unread_messages(&queue));
    client_page.consumed = 7;
    TEST_ASSERT_FALSE(Queue__has_unread_messages(&queue));
    client_page.consumed = 8;
    TEST_ASSERT_FALSE(Queue__has_unread_messages(&queue));
    client_page.consumed = 0;
    server_page.produced = QUEUE_CAPACITY + 1;
    TEST_ASSERT_FALSE(Queue__has_unread_messages(&queue));
    server_page.produced = QUEUE_CAPACITY;
    TEST_ASSERT_TRUE(Queue__has_unread_messages(&queue));
    identity.accepted = 0;
    TEST_ASSERT_FALSE(Queue__has_unread_messages(&queue));
    identity.accepted = 1;
    identity.closed = 1;
    TEST_ASSERT_FALSE(Queue__has_unread_messages(&queue));
}

void test_unread_query_handles_cursor_wraparound(void)
{
    Queue queue = client_queue();
    client_page.consumed = UINT64_MAX;
    server_page.produced = 0;
    TEST_ASSERT_TRUE(Queue__has_unread_messages(&queue));
    client_page.consumed = 0;
    TEST_ASSERT_FALSE(Queue__has_unread_messages(&queue));
}

void test_progress_snapshots_both_directions_and_connection_state(void)
{
    Queue queue = client_queue();
    client_page.produced = 11;
    client_page.consumed = 7;
    server_page.produced = 13;
    server_page.consumed = 9;
    identity.closed = 1;

    QueueProgress progress = Queue__progress(&queue);

    TEST_ASSERT_EQUAL_UINT64(13, progress.incoming_produced);
    TEST_ASSERT_EQUAL_UINT64(9, progress.incoming_consumed);
    TEST_ASSERT_EQUAL_UINT64(11, progress.outgoing_produced);
    TEST_ASSERT_EQUAL_UINT64(7, progress.outgoing_consumed);
    TEST_ASSERT_EQUAL_UINT64(1, progress.accepted);
    TEST_ASSERT_EQUAL_UINT64(1, progress.closed);
    QueueProgress current = Queue__progress(&queue);
    TEST_ASSERT_TRUE(QueueProgress__equals(&progress, &current));
    client_page.produced++;
    current = Queue__progress(&queue);
    TEST_ASSERT_FALSE(QueueProgress__equals(&progress, &current));
}
