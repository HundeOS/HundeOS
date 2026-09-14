#include "unity.h"
#include <stdarg.h>

#define main queue_program_main
#include "../../../../user/programs/queue_test/main.c"
#undef main

static QueueIdentity identity;
static QueuePage client_page;
static QueuePage server_page;
static QueueArrivals arrivals;
static TestQueues server_queues;
static uint64_t close_count;
static uint64_t accept_count;
static bool reject_accept;

static void assert_push_success(QueuePtr queue, ConstQueueMessagePtr message)
{
    match_result(
        Queue__push(queue, message),
        success(status) { TEST_ASSERT_EQUAL(QUEUE_OK, status); },
        error_test_fail()
    );
}

static Queue client_queue(void)
{
    match_result(
        Queue__attach(&identity, &client_page, &server_page, 16, 1),
        success(queue) { return queue; },
        error_test_fail()
    );
    return (Queue){0};
}

void setUp(void)
{
    identity = (QueueIdentity){.connection = 16, .endpoint = 32, .client_pid = 1, .server_pid = 2};
    client_page = (QueuePage){0};
    server_page = (QueuePage){0};
    arrivals = (QueueArrivals){.endpoint = 32};
    match_result(QueueConnectionSlots__set(&arrivals.connections, 0, 16),
        success_do_nothing(),
        error_test_fail()
    );
    server_queues = TestQueues__new();
    close_count = 0;
    accept_count = 0;
    reject_accept = false;
}

void tearDown(void)
{
}

Result(Queue) Queue__accept(QueueConnectionID connection)
{
    accept_count++;
    if (reject_accept)
    {
        return Error(Queue, bstr("Rejected"), QUEUE_CLOSED);
    }
    identity.accepted = 1;
    return Queue__attach(&identity, &client_page, &server_page, connection, 2);
}

QueueCode QueueConnection__close(QueueConnectionID connection)
{
    close_count++;
    identity.closed = 1;
    match_result(QueueConnectionSlots__set(&arrivals.connections, 0, 0),
        success_do_nothing(),
        error_test_fail()
    );
    return QUEUE_OK;
}

Result(QueueArrivalsPtr) Queue__listen(ConstBStringPtr name)
{
    return Success(QueueArrivalsPtr, &arrivals);
}

Result(Queue) Queue__connect(QueueEndpointID endpoint)
{
    return Success(Queue, client_queue());
}

void Queue__wait_readable(QueuePtr queue)
{
    QueueTest__serve_once(&server_queues, &arrivals);
}

void Queue__wait_writable(QueuePtr queue)
{
    QueueTest__serve_once(&server_queues, &arrivals);
}

ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}
void IO__print(ConstBStringPtr text)
{
}
int IO__printf(ConstBStringPtr format, ...)
{
    return 0;
}

void test_queue_program_performs_128_round_trips_on_one_connection(void)
{
    Queue queue = client_queue();
    TEST_ASSERT_EQUAL_INT(0, QueueTest__exchange(&queue));
    TEST_ASSERT_EQUAL_UINT64(1, accept_count);
    TEST_ASSERT_EQUAL_UINT64(128, queue.read);
    TEST_ASSERT_EQUAL_UINT64(128, queue.written);
}

void test_server_cleans_up_closed_connection(void)
{
    QueueTest__serve_once(&server_queues, &arrivals);
    identity.closed = 1;
    TEST_ASSERT_TRUE(QueueTest__serve_once(&server_queues, &arrivals));
    TEST_ASSERT_EQUAL_UINT64(1, close_count);
    TEST_ASSERT_EQUAL_UINT64(0, TestQueues__len(&server_queues));
}

void test_server_rejects_arrival_that_closed_before_accept(void)
{
    reject_accept = true;
    TEST_ASSERT_TRUE(QueueTest__serve_once(&server_queues, &arrivals));
    TEST_ASSERT_EQUAL_UINT64(1, close_count);
    TEST_ASSERT_EQUAL_UINT64(0, TestQueues__len(&server_queues));
}

void test_response_backpressure_retains_unconsumed_request(void)
{
    Queue queue = client_queue();
    QueueTest__serve_once(&server_queues, &arrivals);
    QueueMessage request = QueueMessage__new(0, 1);
    for (uint64_t index = 0; index < QUEUE_CAPACITY; index++)
    {
        assert_push_success(&queue, &request);
        QueueTest__serve_once(&server_queues, &arrivals);
    }
    assert_push_success(&queue, &request);
    QueueCursor consumed = server_page.consumed;
    TEST_ASSERT_FALSE(QueueTest__serve_once(&server_queues, &arrivals));
    TEST_ASSERT_EQUAL_UINT64(consumed, server_page.consumed);
    match_result(Queue__pop(&queue),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(QueueTest__serve_once(&server_queues, &arrivals));
    TEST_ASSERT_EQUAL_UINT64(consumed + 1, server_page.consumed);
}
