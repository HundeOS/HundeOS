#include "unity.h"
#include <stdarg.h>

#define main queue_program_main
#include "../../../../user/programs/queue_test/main.c"
#undef main

static AtomicMessageQueueIdentity identity;
static AtomicMessageQueuePage client_page;
static AtomicMessageQueuePage server_page;
static AtomicMessageQueueArrivals arrivals;
static TestQueues server_queues;
static uint64_t close_count;
static uint64_t accept_count;
static bool reject_accept;

static void assert_push_success(AtomicMessageQueuePtr queue, ConstServiceQueueMessagePtr message)
{
    match_result(
        AtomicMessageQueue__push(queue, message),
        success(status) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_OK, status); },
        error_test_fail()
    );
}

static AtomicMessageQueue client_queue(void)
{
    match_result(
        AtomicMessageQueue__attach(&identity, &client_page, &server_page, 16, 1),
        success(queue) { return queue; },
        error_test_fail()
    );
    return (AtomicMessageQueue){0};
}

void setUp(void)
{
    identity = (AtomicMessageQueueIdentity){.connection = 16, .endpoint = 32, .client_pid = 1, .server_pid = 2};
    client_page = (AtomicMessageQueuePage){0};
    server_page = (AtomicMessageQueuePage){0};
    arrivals = (AtomicMessageQueueArrivals){.endpoint = 32};
    match_result(AtomicMessageQueueConnectionSlots__set(&arrivals.connections, 0, 16),
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

Result(AtomicMessageQueue) atomic_message_queue_accept(AtomicMessageQueueConnectionID connection)
{
    accept_count++;
    if (reject_accept)
    {
        return Error(AtomicMessageQueue, bstr("Rejected"), ATOMIC_MESSAGE_QUEUE_CLOSED);
    }
    identity.accepted = 1;
    return AtomicMessageQueue__attach(&identity, &client_page, &server_page, connection, 2);
}

AtomicMessageQueueCode atomic_message_queue_close(AtomicMessageQueueConnectionID connection)
{
    close_count++;
    identity.closed = 1;
    match_result(AtomicMessageQueueConnectionSlots__set(&arrivals.connections, 0, 0),
        success_do_nothing(),
                 error_test_fail()
    );
    return ATOMIC_MESSAGE_QUEUE_OK;
}

Result(AtomicMessageQueueArrivalsPtr) atomic_message_queue_listen(ConstBStringPtr name)
{
    return Success(AtomicMessageQueueArrivalsPtr, &arrivals);
}

Result(AtomicMessageQueue) atomic_message_queue_connect(AtomicMessageQueueEndpointID endpoint)
{
    return Success(AtomicMessageQueue, client_queue());
}

void atomic_message_queue_wait_readable(AtomicMessageQueuePtr queue)
{
    QueueTest__serve_once(&server_queues, &arrivals);
}

void atomic_message_queue_wait_writable(AtomicMessageQueuePtr queue)
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
    AtomicMessageQueue queue = client_queue();
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
    AtomicMessageQueue queue = client_queue();
    QueueTest__serve_once(&server_queues, &arrivals);
    ServiceQueueMessage request = ServiceQueueMessage__new(0, 1);
    for (uint64_t index = 0; index < ATOMIC_MESSAGE_QUEUE_CAPACITY; index++)
    {
        assert_push_success(&queue, &request);
        QueueTest__serve_once(&server_queues, &arrivals);
    }
    assert_push_success(&queue, &request);
    AtomicMessageQueueCursor consumed = server_page.consumed;
    TEST_ASSERT_FALSE(QueueTest__serve_once(&server_queues, &arrivals));
    TEST_ASSERT_EQUAL_UINT64(consumed, server_page.consumed);
    match_result(AtomicMessageQueue__pop(&queue),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(QueueTest__serve_once(&server_queues, &arrivals));
    TEST_ASSERT_EQUAL_UINT64(consumed + 1, server_page.consumed);
}
