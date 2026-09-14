#include "unity.h"
#include "ipc/queue.h"

static QueueIdentity identity;
static QueuePage client_page;
static QueuePage server_page;

void setUp(void)
{
    identity = (QueueIdentity){.connection = 17, .client_pid = 4, .server_pid = 5, .accepted = 1};
    client_page = (QueuePage){0};
    server_page = (QueuePage){0};
}

void tearDown(void)
{
}

static Queue client_queue(void)
{
    match_result(
        Queue__attach(&identity, &client_page, &server_page, 17, 4),
        success(queue) { return queue; },
        error_test_fail()
    );
    return (Queue){0};
}

void test_new_constructs_a_raw_queue_with_zeroed_private_cursors(void)
{
    Queue queue = Queue__new(&identity, &server_page, &client_page, 23, 7);
    TEST_ASSERT_EQUAL_PTR(&identity, queue.identity);
    TEST_ASSERT_EQUAL_PTR(&server_page, queue.outgoing);
    TEST_ASSERT_EQUAL_PTR(&client_page, queue.incoming);
    TEST_ASSERT_EQUAL_UINT64(23, queue.connection);
    TEST_ASSERT_EQUAL_UINT64(7, queue.peer_pid);
    TEST_ASSERT_EQUAL_UINT64(0, queue.read);
    TEST_ASSERT_EQUAL_UINT64(0, queue.written);
    TEST_ASSERT_EQUAL_UINT64(0, queue.acknowledged);
}

void test_attach_rejects_invalid_connection_and_participant(void)
{
    match_result(
        Queue__attach(&identity, &client_page, &server_page, 18, 4),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_INVALID, code); }
    );
    match_result(
        Queue__attach(&identity, &client_page, &server_page, 17, 6),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_INVALID, code); }
    );
}

void test_push_returns_success_result_and_pop_authenticates_sender(void)
{
    Queue client = client_queue();
    QueueMessage sent = QueueMessage__new(999, 7);
    match_result(
        Queue__push(&client, &sent),
        success(status) { TEST_ASSERT_EQUAL(QUEUE_OK, status); },
        error_test_fail()
    );
    match_result(
        Queue__attach(&identity, &client_page, &server_page, 17, 5),
        success(server)
        {
            match_result(
                Queue__pop(&server),
                success(received)
                {
                    TEST_ASSERT_EQUAL_UINT64(4, received.sender_pid);
                    TEST_ASSERT_EQUAL_UINT64(7, received.kind);
                },
                error_test_fail()
            );
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
        error(code, error_message) { TEST_ASSERT_EQUAL(QUEUE_PENDING, code); }
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
        error(code, error_message) { TEST_ASSERT_EQUAL(QUEUE_FULL, code); }
    );
    server_page.consumed = UINT64_MAX;
    match_result(
        Queue__push(&queue, &message),
        success_test_fail(),
        error(code, error_message) { TEST_ASSERT_EQUAL(QUEUE_INVALID, code); }
    );
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
        some(connection) { TEST_ASSERT_EQUAL_UINT64(2047, connection); },
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
        some(connection) { TEST_ASSERT_EQUAL_UINT64(67, connection); },
        none_test_fail()
    );
    match_optional(QueueArrivals__connection(&arrivals, QUEUE_LIMIT),
        some_test_fail(),
        none_do_nothing()
    );
}
