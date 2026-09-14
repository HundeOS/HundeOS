#include "unity.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/syscall/service_handler.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/page_table.h"
#include "../stubs/kalloc.h"
#include "../stubs/panic.h"
#include "../stubs/process_context_layout.h"

static KernelPtr kernel;
static ProcessPtr server;
static ProcessPtr client;
static ProcessPtr other;
static QueuesPtr queues;

void setUp(void)
{
    KAllocStub__set_free_failure(false);
    clear_panic_state();
    KAllocStub__reset_allocation_tracking();
    kernel = init_test_kernel_without_filesystem();
    server = dummy_process(10, 0, PROC_STATE_READY);
    client = dummy_process(20, 0, PROC_STATE_READY);
    other = dummy_process(30, 0, PROC_STATE_READY);
    server->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(server);
    Scheduler__schedule_on_cpu(kernel->scheduler, server, 0);
    Scheduler__schedule_on_cpu(kernel->scheduler, client, 0);
    Scheduler__schedule_on_cpu(kernel->scheduler, other, 0);
    queues = &kernel->scheduler->queues;
    if (!kernel->service_directory.directory.valid)
    {
        match_result(KernelServiceDirectory__init(&kernel->service_directory),
            success_do_nothing(),
            error_test_fail()
        );
    }
}

void tearDown(void)
{
    KAllocStub__set_free_failure(false);
    TEST_ASSERT_FALSE(panic_occurred());
}

static QueueEndpointID find_endpoint(ConstBStringPtr name)
{
    match_optional(
        ServiceDirectory__find_endpoint(KernelServiceDirectory__kernel_directory(&kernel->service_directory), name),
        some(endpoint) { return endpoint; },
        none_test_fail()
    );
    return 0;
}

static QueueEndpointID listen_queue(void)
{
    match_result(
        Queues__listen(queues, server, &bstr("protected-test")),
        success(address) { TEST_ASSERT_EQUAL_HEX64(QUEUE_LISTENER_BASE, address); },
        error_test_fail()
    );
    return find_endpoint(&bstr("protected-test"));
}

static void assert_push_success(QueuePtr queue, ConstQueueMessagePtr message)
{
    match_result(
        Queue__push(queue, message),
        success(status) { TEST_ASSERT_EQUAL(QUEUE_OK, status); },
        error_test_fail()
    );
}

static void assert_push_error(QueuePtr queue, ConstQueueMessagePtr message, QueueCode expected)
{
    match_result(
        Queue__push(queue, message),
        success_test_fail(),
        error(code, error_message) { TEST_ASSERT_EQUAL(expected, code); }
    );
}

static void assert_endpoint_missing(ConstBStringPtr name)
{
    match_optional(
        ServiceDirectory__find_endpoint(KernelServiceDirectory__kernel_directory(&kernel->service_directory), name),
        some_test_fail(),
        none_do_nothing()
    );
}

static void assert_arrival_connection(QueueArrivalsPtr arrivals, QueueSlotIndex index, QueueConnectionID expected)
{
    match_optional(
        QueueArrivals__connection(arrivals, index),
        some(connection) { TEST_ASSERT_EQUAL_UINT64(expected, connection); },
        none_test_fail()
    );
}

static void assert_arrival_index_missing(QueueArrivalsPtr arrivals, QueueSlotIndex index)
{
    match_optional(QueueArrivals__connection(arrivals, index),
        some_test_fail(),
        none_do_nothing()
    );
}

static QueueConnectionID connect_queue(QueueEndpointID endpoint, ProcessPtr process)
{
    match_result(
        Scheduler__queue_operation(kernel->scheduler, process, SERVICE_QUEUE_CONNECT, endpoint),
        success(id) { return id; },
        error_test_fail()
    );
    return 0;
}

static UserAddress connection_memory(QueueConnectionID id)
{
    match_optional(
        QueueConnections__ptr_at(&queues->connections, id % QUEUE_LIMIT),
        some(connection) { return connection->memory; },
        none() { TEST_FAIL(); }
    );
    return NULL_USER_ADDRESS;
}

static Queue attach_queue(QueueConnectionID id, ProcessPtr process)
{
    UserAddress memory = connection_memory(id);
    Address base = memory.kernel_addr.virtual_addr;
    match_result(
        Queue__attach(ptr(base, QueueIdentity), ptr((base + QUEUE_PAGE_SIZE), QueuePage),
                      ptr((base + 2 * QUEUE_PAGE_SIZE), QueuePage), id, process->pid),
        success(queue) { return queue; },
        error_test_fail()
    );
    return (Queue){0};
}

static void accept_queue(QueueConnectionID id)
{
    match_result(Scheduler__queue_operation(kernel->scheduler, server, SERVICE_QUEUE_ACCEPT, id),
        success_do_nothing(),
                 error_test_fail()
    );
}

static void close_queue(QueueConnectionID id, ProcessPtr process)
{
    match_result(Scheduler__queue_operation(kernel->scheduler, process, SERVICE_QUEUE_CLOSE, id),
        success_do_nothing(),
                 error_test_fail()
    );
}

static bool mapped(ProcessPtr process, Address address, bool write)
{
    size_t size =
        address >= QUEUE_LISTENER_BASE && address < QUEUE_LISTENER_BASE + QUEUE_LISTENER_LIMIT * QUEUE_ARRIVALS_SIZE
            ? QUEUE_ARRIVALS_SIZE
            : QUEUE_PAGE_SIZE;
    return check_address_mapped(ProcessPageTable__root_table(&process->page_table), address, true, write, false, size);
}

void test_queue_pages_have_isolated_asymmetric_permissions(void)
{
    QueueEndpointID endpoint = listen_queue();
    TEST_ASSERT_NOT_EQUAL(0, endpoint);
    TEST_ASSERT_TRUE(mapped(server, QUEUE_LISTENER_BASE, false));
    TEST_ASSERT_FALSE(mapped(client, QUEUE_LISTENER_BASE, false));
    QueueConnectionID id = connect_queue(endpoint, client);
    Address address = QueueLayout__address(id);
    TEST_ASSERT_TRUE(mapped(client, address, false));
    TEST_ASSERT_TRUE(mapped(server, address, false));
    TEST_ASSERT_TRUE(mapped(client, address + QUEUE_PAGE_SIZE, true));
    TEST_ASSERT_TRUE(mapped(server, address + QUEUE_PAGE_SIZE, false));
    TEST_ASSERT_TRUE(mapped(client, address + 2 * QUEUE_PAGE_SIZE, false));
    TEST_ASSERT_TRUE(mapped(server, address + 2 * QUEUE_PAGE_SIZE, true));
    for (uint64_t page = 0; page < 3; page++)
    {
        TEST_ASSERT_FALSE(mapped(other, address + page * QUEUE_PAGE_SIZE, true));
        TEST_ASSERT_FALSE(mapped(other, address + page * QUEUE_PAGE_SIZE, false));
    }
}

void test_repeated_messages_use_kernel_identity_not_spoofed_sender(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    Queue outgoing = attach_queue(id, client);
    Queue incoming = attach_queue(id, server);
    QueueMessage message = QueueMessage__new(9999, 42);
    assert_push_error(&outgoing, &message, QUEUE_PENDING);
    accept_queue(id);
    for (uint64_t sequence = 0; sequence < 100; sequence++)
    {
        message.arg0 = sequence;
        assert_push_success(&outgoing, &message);
        match_result(
            Queue__pop(&incoming),
            success(received)
            {
                TEST_ASSERT_EQUAL_UINT64(client->pid, received.sender_pid);
                TEST_ASSERT_EQUAL_UINT64(sequence, received.arg0);
                assert_push_success(&incoming, &received);
            },
            error_test_fail()
        );
        match_result(
            Queue__pop(&outgoing),
            success(reply) { TEST_ASSERT_EQUAL_UINT64(server->pid, reply.sender_pid); },
            error_test_fail()
        );
    }
}

void test_full_and_corrupted_queue_do_not_affect_another_client(void)
{
    QueueEndpointID endpoint = listen_queue();
    QueueConnectionID id = connect_queue(endpoint, client);
    QueueConnectionID second_id = connect_queue(endpoint, other);
    accept_queue(id);
    accept_queue(second_id);
    Queue outgoing = attach_queue(id, client);
    Queue incoming = attach_queue(id, server);
    Queue second = attach_queue(second_id, other);
    Queue second_server = attach_queue(second_id, server);
    QueueMessage message = QueueMessage__new(0, 1);
    for (uint64_t index = 0; index < QUEUE_CAPACITY; index++)
    {
        assert_push_success(&outgoing, &message);
    }
    assert_push_error(&outgoing, &message, QUEUE_FULL);
    match_result(Queue__pop(&incoming),
        success_do_nothing(),
        error_test_fail()
    );
    assert_push_success(&outgoing, &message);
    __atomic_store_n(&outgoing.outgoing->produced, UINT64_MAX, __ATOMIC_RELEASE);
    match_result(
        Queue__pop(&incoming),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_INVALID, code); }
    );
    __atomic_store_n(&outgoing.outgoing->consumed, UINT64_MAX, __ATOMIC_RELEASE);
    assert_push_error(&incoming, &message, QUEUE_INVALID);
    assert_push_success(&second, &message);
    match_result(Queue__pop(&second_server),
        success_do_nothing(),
        error_test_fail()
    );
}

void test_close_retains_survivor_pages_until_its_acknowledgement(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    accept_queue(id);
    Queue survivor = attach_queue(id, server);
    size_t before = KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE);
    close_queue(id, client);
    TEST_ASSERT_EQUAL(QUEUE_CLOSED, Queue__status(&survivor));
    TEST_ASSERT_FALSE(mapped(client, QueueLayout__address(id), false));
    TEST_ASSERT_TRUE(mapped(server, QueueLayout__address(id), false));
    TEST_ASSERT_EQUAL_UINT64(before, KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE));
    close_queue(id, server);
    TEST_ASSERT_EQUAL_UINT64(before + 1, KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE));
    TEST_ASSERT_FALSE(mapped(server, QueueLayout__address(id), false));
}

void test_close_commits_detachment_when_backing_free_fails_and_retries_later(void)
{
    QueueEndpointID endpoint = listen_queue();
    QueueConnectionID id = connect_queue(endpoint, client);
    close_queue(id, client);
    size_t before = KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE);
    KAllocStub__set_free_failure(true);
    close_queue(id, server);
    TEST_ASSERT_FALSE(mapped(server, QueueLayout__address(id), false));
    TEST_ASSERT_EQUAL_UINT64(before, KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE));
    match_result(
        Scheduler__queue_operation(kernel->scheduler, server, SERVICE_QUEUE_CLOSE, id),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_DENIED, code); }
    );
    QueueConnectionID other_id = connect_queue(endpoint, other);
    TEST_ASSERT_NOT_EQUAL(QueueLayout__address(id), QueueLayout__address(other_id));
    KAllocStub__set_free_failure(false);
    QueueConnectionID replacement = connect_queue(endpoint, client);
    TEST_ASSERT_EQUAL_UINT64(before + 1, KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE));
    TEST_ASSERT_NOT_EQUAL(id, replacement);
}

void test_exit_marks_peer_closed_and_removes_listener_without_losing_state(void)
{
    QueueEndpointID endpoint = listen_queue();
    QueueConnectionID id = connect_queue(endpoint, client);
    Queue survivor = attach_queue(id, client);
    match_result(Scheduler__exit(kernel->scheduler, server, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(QUEUE_CLOSED, Queue__status(&survivor));
    assert_endpoint_missing(&bstr("protected-test"));
    match_result(
        Scheduler__queue_operation(kernel->scheduler, other, SERVICE_QUEUE_CONNECT, endpoint),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_INVALID, code); }
    );
    close_queue(id, client);
}

void test_signal_keeps_queue_mapped_until_owner_cpu_performs_teardown(void)
{
    QueueEndpointID endpoint = listen_queue();
    QueueConnectionID id = connect_queue(endpoint, client);
    accept_queue(id);
    Queue survivor = attach_queue(id, client);
    ProcessID pid = server->pid;
    match_result(Scheduler__queue_signal(kernel->scheduler, pid, PROCESS_SIGNAL_KILL),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_EQUAL(QUEUE_OK, Queue__status(&survivor));
    TEST_ASSERT_TRUE(mapped(server, QueueLayout__address(id), false));
    TEST_ASSERT_EQUAL_UINT64(endpoint, find_endpoint(&bstr("protected-test")));

    ProcessContext context = {0};
    Scheduler__start(kernel->scheduler, 0);
    for (size_t tick = 0; tick < 4; tick++)
    {
        Scheduler__preempt(kernel->scheduler, 0, &context);
    }
    match_optional(Scheduler__find_process_by_pid(kernel->scheduler, pid),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_EQUAL(QUEUE_CLOSED, Queue__status(&survivor));
    TEST_ASSERT_TRUE(mapped(client, QueueLayout__address(id), false));
    assert_endpoint_missing(&bstr("protected-test"));
    size_t before = KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE);
    close_queue(id, client);
    TEST_ASSERT_EQUAL_UINT64(before + 1, KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE));
    TEST_ASSERT_FALSE(mapped(client, QueueLayout__address(id), false));
}

void test_client_exit_before_accept_keeps_arrival_for_server_cleanup(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    match_result(Scheduler__exit(kernel->scheduler, client, 0),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(
        Scheduler__queue_operation(kernel->scheduler, server, SERVICE_QUEUE_ACCEPT, id),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_CLOSED, code); }
    );
    close_queue(id, server);
}

void test_only_owner_can_accept_or_close_and_stale_ids_cannot_close_reused_slot(void)
{
    QueueEndpointID endpoint = listen_queue();
    QueueConnectionID id = connect_queue(endpoint, client);
    match_result(
        Scheduler__queue_operation(kernel->scheduler, other, SERVICE_QUEUE_ACCEPT, id),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_DENIED, code); }
    );
    match_result(
        Scheduler__queue_operation(kernel->scheduler, other, SERVICE_QUEUE_CLOSE, id),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_DENIED, code); }
    );
    close_queue(id, server);
    close_queue(id, client);
    QueueConnectionID replacement = connect_queue(endpoint, client);
    TEST_ASSERT_NOT_EQUAL(id, replacement);
    TEST_ASSERT_EQUAL_HEX64(QueueLayout__address(id), QueueLayout__address(replacement));
    match_result(
        Scheduler__queue_operation(kernel->scheduler, client, SERVICE_QUEUE_CLOSE, id),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_INVALID, code); }
    );
}

void test_listener_permission_duplicate_name_and_connection_admission(void)
{
    match_result(
        Queues__listen(queues, client, &bstr("denied")),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_DENIED, code); }
    );
    QueueEndpointID endpoint = listen_queue();
    other->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(other);
    match_result(
        Queues__listen(queues, other, &bstr("protected-test")),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_DEVICE_ALREADY_REGISTERED, code); }
    );
    for (uint64_t index = 0; index < 4; index++)
    {
        connect_queue(endpoint, client);
    }
    match_result(
        Scheduler__queue_operation(kernel->scheduler, client, SERVICE_QUEUE_CONNECT, endpoint),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_FULL, code); }
    );
    connect_queue(endpoint, other);
}

void test_allocation_failure_publishes_no_connection_and_invalid_name_pointer_is_rejected(void)
{
    QueueEndpointID endpoint = listen_queue();
    KAllocStub__set_user_alloc_failure(true);
    match_result(
        Scheduler__queue_operation(kernel->scheduler, client, SERVICE_QUEUE_CONNECT, endpoint),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_NO_MEMORY, code); }
    );
    TEST_ASSERT_EQUAL_UINT64(0, QueueConnections__len(&queues->connections));
    Optional(ProcessPtr) caller = Some(ProcessPtr, server);
    match_result(
        Service__handler(kernel, &caller, SERVICE_QUEUE_LISTEN, UINT64_MAX, 10, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_INVALID, code); }
    );
}

void test_shared_memory_manager_can_broker_more_than_four_channels(void)
{
    QueueEndpointID endpoint = listen_queue();
    other->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(other);
    for (uint64_t index = 0; index < 6; index++)
    {
        connect_queue(endpoint, other);
    }
}

void test_service_directory_accepts_1024_entries_and_reuses_a_removed_slot(void)
{
    TEST_ASSERT_EQUAL_UINT64(1024, HUND_SERVICE_MAX_SERVICES);
    TEST_ASSERT_EQUAL_UINT64(HUND_SERVICE_MAX_SERVICES, QUEUE_LISTENER_LIMIT);
    for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
    {
        BString name = BString__sprintf(&bstr("service-%llu"), index);
        match_result(KernelServiceDirectory__publish_endpoint(&kernel->service_directory, server, &name, index + 1),
                     success_do_nothing(),
            error_test_fail()
        );
    }
    TEST_ASSERT_EQUAL_UINT64(1024, find_endpoint(&bstr("service-1023")));
    match_result(
        KernelServiceDirectory__publish_endpoint(&kernel->service_directory, server, &bstr("overflow"), 2048),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_FULL, code); }
    );
    KernelServiceDirectory__remove_endpoint(&kernel->service_directory, 1024);
    assert_endpoint_missing(&bstr("service-1023"));
    match_result(
        KernelServiceDirectory__publish_endpoint(&kernel->service_directory, server, &bstr("replacement"), 2048),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(2048, find_endpoint(&bstr("replacement")));
}

void test_all_1024_connection_slots_are_usable_and_recycled_with_fresh_handles(void)
{
    TEST_ASSERT_EQUAL(1024, QUEUE_LIMIT);
    QueueEndpointID endpoint = listen_queue();
    other->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(other);
    QueueConnectionID last = 0;
    for (QueueSlotIndex index = 0; index < QUEUE_LIMIT; index++)
    {
        last = connect_queue(endpoint, other);
        TEST_ASSERT_EQUAL_UINT64(index, last % QUEUE_LIMIT);
    }
    TEST_ASSERT_TRUE(mapped(other, QueueLayout__address(last), false));
    TEST_ASSERT_TRUE(mapped(server, QueueLayout__address(last), false));
    match_optional(
        QueueListeners__ptr_at(&queues->listeners, 0),
        some(listener)
        {
            QueueArrivalsPtr arrivals = k_v_ptr(listener->arrivals, QueueArrivals);
            assert_arrival_connection(arrivals, QUEUE_LIMIT - 1, last);
        },
        none_test_fail()
    );
    match_result(
        Queues__connect(queues, client, endpoint),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_FULL, code); }
    );
    accept_queue(last);
    close_queue(last, other);
    close_queue(last, server);
    QueueConnectionID replacement = connect_queue(endpoint, client);
    TEST_ASSERT_EQUAL_UINT64(last % QUEUE_LIMIT, replacement % QUEUE_LIMIT);
    TEST_ASSERT_NOT_EQUAL(last, replacement);
    match_result(
        Queues__accept(queues, server, last),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(QUEUE_DENIED, code); }
    );
    Queues__exit(queues, other);
    Queues__exit(queues, client);
    Queues__exit(queues, server);
    TEST_ASSERT_FALSE(mapped(server, QUEUE_LISTENER_BASE, false));
}

void test_queue_syscalls_copy_tracked_name_and_bind_caller_identity(void)
{
    BString name = bstr("syscall-queue");
    UserAddress memory = NULL_USER_ADDRESS;
    match_result(
        Process__alloc_user_pages(server, QUEUE_PAGE_SIZE),
        success(address) { memory = address; },
        error_test_fail()
    );
    Kernel__memcopy((Address)BString__cstr(&name), memory.kernel_addr.virtual_addr, BString__length(&name));
    Optional(ProcessPtr) caller = Some(ProcessPtr, server);
    match_result(
        Service__handler(kernel, &caller, SERVICE_QUEUE_LISTEN, memory.virtual_addr, BString__length(&name), 0, 0),
        success(address) { TEST_ASSERT_EQUAL_HEX64(QUEUE_LISTENER_BASE, address); },
        error_test_fail()
    );
    QueueEndpointID endpoint = find_endpoint(&name);
    caller = Some(ProcessPtr, client);
    match_result(
        Service__handler(kernel, &caller, SERVICE_QUEUE_CONNECT, endpoint, 9999, 0, 0),
        success(id)
        {
            Queue queue = attach_queue(id, client);
            TEST_ASSERT_EQUAL_UINT64(client->pid, queue.identity->client_pid);
            TEST_ASSERT_EQUAL_UINT64(endpoint, queue.identity->endpoint);
        },
        error_test_fail()
    );
}

void test_arrivals_are_durable_and_backing_is_zeroed_before_publication(void)
{
    KAllocStub__set_alloc_fill(true, 0xa5);
    QueueConnectionID id = connect_queue(listen_queue(), client);
    match_optional(
        QueueListeners__ptr_at(&queues->listeners, 0),
        some(listener)
        {
            QueueArrivalsPtr arrivals = k_v_ptr(listener->arrivals, QueueArrivals);
            assert_arrival_connection(arrivals, id % QUEUE_LIMIT, id);
            QueueEpoch epoch = QueueArrivals__epoch(arrivals);
            close_queue(id, client);
            assert_arrival_connection(arrivals, id % QUEUE_LIMIT, id);
            TEST_ASSERT_TRUE(QueueArrivals__epoch(arrivals) > epoch);
            close_queue(id, server);
            assert_arrival_connection(arrivals, id % QUEUE_LIMIT, 0);
        },
        none() { TEST_FAIL(); }
    );
    QueueConnectionID replacement = connect_queue(find_endpoint(&bstr("protected-test")), client);
    Queue queue = attach_queue(replacement, client);
    TEST_ASSERT_EQUAL_UINT64(0, queue.outgoing->produced);
    TEST_ASSERT_EQUAL_UINT64(0, queue.incoming->consumed);
    TEST_ASSERT_EQUAL_UINT64(0, queue.outgoing->messages[0].arg0);
    KAllocStub__set_alloc_fill(false, 0);
}

void test_arrival_iteration_keeps_connection_slots_stable_after_close(void)
{
    QueueEndpointID endpoint = listen_queue();
    QueueConnectionID first = connect_queue(endpoint, client);
    QueueConnectionID second = connect_queue(endpoint, other);
    close_queue(first, client);
    close_queue(first, server);
    match_optional(
        QueueListeners__ptr_at(&queues->listeners, 0),
        some(listener)
        {
            QueueArrivalsPtr arrivals = k_v_ptr(listener->arrivals, QueueArrivals);
            QueueConnectionID occupied = 0;
            for_each_atomic_fa_ptr(QueueConnectionSlots, &arrivals->connections, index, id, {
                if (id == 0)
                {
                    continue;
                }
                TEST_ASSERT_EQUAL_UINT64(second, id);
                TEST_ASSERT_EQUAL_UINT64(second % QUEUE_LIMIT, index);
                occupied++;
            });
            TEST_ASSERT_EQUAL_UINT64(1, occupied);
            assert_arrival_connection(arrivals, first % QUEUE_LIMIT, 0);
            assert_arrival_index_missing(arrivals, QUEUE_LIMIT);
        },
        none_test_fail()
    );
    close_queue(second, other);
    close_queue(second, server);
}
