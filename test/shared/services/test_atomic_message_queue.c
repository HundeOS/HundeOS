#include "unity.h"
#include "services/atomic_message_queue.h"

static AtomicMessageQueueIdentity identity;
static AtomicMessageQueuePage client_page;
static AtomicMessageQueuePage server_page;

void setUp(void)
{
    identity = (AtomicMessageQueueIdentity){.connection = 17, .client_pid = 4, .server_pid = 5, .accepted = 1};
    client_page = (AtomicMessageQueuePage){0};
    server_page = (AtomicMessageQueuePage){0};
}

void tearDown(void)
{
}

static AtomicMessageQueue client_queue(void)
{
    match_result(
        AtomicMessageQueue__attach(&identity, &client_page, &server_page, 17, 4),
        success(queue) { return queue; },
        error_test_fail()
    );
    return (AtomicMessageQueue){0};
}

void test_new_constructs_a_raw_queue_with_zeroed_private_cursors(void)
{
    AtomicMessageQueue queue = AtomicMessageQueue__new(&identity, &server_page, &client_page, 23, 7);
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
        AtomicMessageQueue__attach(&identity, &client_page, &server_page, 18, 4),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_INVALID, code); }
    );
    match_result(
        AtomicMessageQueue__attach(&identity, &client_page, &server_page, 17, 6),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_INVALID, code); }
    );
}

void test_push_returns_success_result_and_pop_authenticates_sender(void)
{
    AtomicMessageQueue client = client_queue();
    ServiceQueueMessage sent = ServiceQueueMessage__new(999, 7);
    match_result(
        AtomicMessageQueue__push(&client, &sent),
        success(status) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_OK, status); },
        error_test_fail()
    );
    match_result(
        AtomicMessageQueue__attach(&identity, &client_page, &server_page, 17, 5),
        success(server)
        {
            match_result(
                AtomicMessageQueue__pop(&server),
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
    AtomicMessageQueue queue = client_queue();
    ServiceQueueMessage message = ServiceQueueMessage__new(0, 1);
    identity.accepted = 0;
    match_result(
        AtomicMessageQueue__push(&queue, &message),
        success_test_fail(),
        error(code, error_message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_PENDING, code); }
    );
    identity.accepted = 1;
    for (uint64_t index = 0; index < ATOMIC_MESSAGE_QUEUE_CAPACITY; index++)
    {
        match_result(AtomicMessageQueue__push(&queue, &message),
            success_do_nothing(),
            error_test_fail()
        );
    }
    match_result(
        AtomicMessageQueue__push(&queue, &message),
        success_test_fail(),
        error(code, error_message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_FULL, code); }
    );
    server_page.consumed = UINT64_MAX;
    match_result(
        AtomicMessageQueue__push(&queue, &message),
        success_test_fail(),
        error(code, error_message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_INVALID, code); }
    );
}

void test_layout_and_arrival_access_have_explicit_bounds(void)
{
    AtomicMessageQueueArrivals arrivals = {0};
    match_result(AtomicMessageQueueConnectionSlots__set(&arrivals.connections, 3, 67),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_EQUAL_HEX64(ATOMIC_MESSAGE_QUEUE_CONNECTION_BASE + ATOMIC_MESSAGE_QUEUE_CONNECTION_SIZE,
                            AtomicMessageQueueLayout__address(17));
    match_optional(
        AtomicMessageQueueArrivals__connection(&arrivals, 3),
        some(connection) { TEST_ASSERT_EQUAL_UINT64(67, connection); },
        none_test_fail()
    );
    match_optional(AtomicMessageQueueArrivals__connection(&arrivals, ATOMIC_MESSAGE_QUEUE_LIMIT),
        some_test_fail(),
                   none_do_nothing()
    );
}

void test_directory_queries_return_optional_endpoint_and_service_pid(void)
{
    ServiceDirectory directory = {0};
    directory.entries[0].present = 1;
    directory.entries[0].generation = 41;
    directory.entries[0].service_pid = 9;
    ServiceDirectoryName__copy_from_bstring(&directory.entries[0].name, &bstr("protected"));
    match_optional(
        ServiceDirectory__find_endpoint(&directory, &bstr("protected")),
        some(endpoint) { TEST_ASSERT_EQUAL_UINT64(41, endpoint); },
        none_test_fail()
    );
    match_optional(
        ServiceDirectory__find_service_pid(&directory, &bstr("protected")),
        some(pid) { TEST_ASSERT_EQUAL_UINT64(9, pid); },
        none_test_fail()
    );
    match_optional(ServiceDirectory__find_endpoint(&directory, &bstr("missing")),
        some_test_fail(),
        none_do_nothing()
    );
}
