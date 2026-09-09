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
static AtomicMessageQueuesPtr queues;

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
    queues = &kernel->scheduler->atomic_message_queues;
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

static AtomicMessageQueueEndpointID find_endpoint(ConstBStringPtr name)
{
    match_optional(
        ServiceDirectory__find_endpoint(KernelServiceDirectory__kernel_directory(&kernel->service_directory), name),
        some(endpoint) { return endpoint; },
        none_test_fail()
    );
    return 0;
}

static AtomicMessageQueueEndpointID listen_queue(void)
{
    match_result(
        AtomicMessageQueues__listen(queues, server, &bstr("protected-test")),
        success(address) { TEST_ASSERT_EQUAL_HEX64(ATOMIC_MESSAGE_QUEUE_LISTENER_BASE, address); },
        error_test_fail()
    );
    return find_endpoint(&bstr("protected-test"));
}

static void assert_push_success(AtomicMessageQueuePtr queue, ConstServiceQueueMessagePtr message)
{
    match_result(
        AtomicMessageQueue__push(queue, message),
        success(status) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_OK, status); },
        error_test_fail()
    );
}

static void assert_push_error(AtomicMessageQueuePtr queue, ConstServiceQueueMessagePtr message,
                              AtomicMessageQueueCode expected)
{
    match_result(
        AtomicMessageQueue__push(queue, message),
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

static void assert_arrival_connection(AtomicMessageQueueArrivalsPtr arrivals, AtomicMessageQueueSlotIndex index,
                                      AtomicMessageQueueConnectionID expected)
{
    match_optional(
        AtomicMessageQueueArrivals__connection(arrivals, index),
        some(connection) { TEST_ASSERT_EQUAL_UINT64(expected, connection); },
        none_test_fail()
    );
}

static void assert_arrival_index_missing(AtomicMessageQueueArrivalsPtr arrivals, AtomicMessageQueueSlotIndex index)
{
    match_optional(AtomicMessageQueueArrivals__connection(arrivals, index),
        some_test_fail(),
        none_do_nothing()
    );
}

static AtomicMessageQueueConnectionID connect_queue(AtomicMessageQueueEndpointID endpoint, ProcessPtr process)
{
    match_result(
        Scheduler__atomic_message_queue_operation(kernel->scheduler, process, SERVICE_ATOMIC_MESSAGE_QUEUE_CONNECT,
                                                  endpoint),
        success(id) { return id; },
        error_test_fail()
    );
    return 0;
}

static UserAddress connection_memory(AtomicMessageQueueConnectionID id)
{
    match_optional(
        AtomicMessageQueueConnections__ptr_at(&queues->connections, id % ATOMIC_MESSAGE_QUEUE_LIMIT),
        some(connection) { return connection->memory; },
        none() { TEST_FAIL(); }
    );
    return NULL_USER_ADDRESS;
}

static AtomicMessageQueue attach_queue(AtomicMessageQueueConnectionID id, ProcessPtr process)
{
    UserAddress memory = connection_memory(id);
    Address base = memory.kernel_addr.virtual_addr;
    match_result(
        AtomicMessageQueue__attach(ptr(base, AtomicMessageQueueIdentity),
                                   ptr((base + HUND_SERVICE_PAGE_SIZE), AtomicMessageQueuePage),
                                   ptr((base + 2 * HUND_SERVICE_PAGE_SIZE), AtomicMessageQueuePage), id, process->pid),
        success(queue) { return queue; },
        error_test_fail()
    );
    return (AtomicMessageQueue){0};
}

static void accept_queue(AtomicMessageQueueConnectionID id)
{
    match_result(
        Scheduler__atomic_message_queue_operation(kernel->scheduler, server, SERVICE_ATOMIC_MESSAGE_QUEUE_ACCEPT, id),
        success_do_nothing(),
        error_test_fail()
    );
}

static void close_queue(AtomicMessageQueueConnectionID id, ProcessPtr process)
{
    match_result(
        Scheduler__atomic_message_queue_operation(kernel->scheduler, process, SERVICE_ATOMIC_MESSAGE_QUEUE_CLOSE, id),
        success_do_nothing(),
        error_test_fail()
    );
}

static bool mapped(ProcessPtr process, Address address, bool write)
{
    return check_address_mapped(ProcessPageTable__root_table(&process->page_table), address, true, write, false,
                                HUND_SERVICE_PAGE_SIZE);
}

void test_atomic_message_queue_pages_have_isolated_asymmetric_permissions(void)
{
    AtomicMessageQueueEndpointID endpoint = listen_queue();
    TEST_ASSERT_NOT_EQUAL(0, endpoint);
    TEST_ASSERT_TRUE(mapped(server, ATOMIC_MESSAGE_QUEUE_LISTENER_BASE, false));
    TEST_ASSERT_FALSE(mapped(client, ATOMIC_MESSAGE_QUEUE_LISTENER_BASE, false));
    AtomicMessageQueueConnectionID id = connect_queue(endpoint, client);
    Address address = AtomicMessageQueueLayout__address(id);
    TEST_ASSERT_TRUE(mapped(client, address, false));
    TEST_ASSERT_TRUE(mapped(server, address, false));
    TEST_ASSERT_TRUE(mapped(client, address + HUND_SERVICE_PAGE_SIZE, true));
    TEST_ASSERT_TRUE(mapped(server, address + HUND_SERVICE_PAGE_SIZE, false));
    TEST_ASSERT_TRUE(mapped(client, address + 2 * HUND_SERVICE_PAGE_SIZE, false));
    TEST_ASSERT_TRUE(mapped(server, address + 2 * HUND_SERVICE_PAGE_SIZE, true));
    for (uint64_t page = 0; page < 3; page++)
    {
        TEST_ASSERT_FALSE(mapped(other, address + page * HUND_SERVICE_PAGE_SIZE, true));
        TEST_ASSERT_FALSE(mapped(other, address + page * HUND_SERVICE_PAGE_SIZE, false));
    }
    match_optional(
        ServiceDirectory__find_queue(KernelServiceDirectory__kernel_directory(&kernel->service_directory),
                                     &bstr("protected-test")),
        some(queue) { TEST_FAIL(); },
        none_do_nothing()
    );
}

void test_repeated_messages_use_kernel_identity_not_spoofed_sender(void)
{
    AtomicMessageQueueConnectionID id = connect_queue(listen_queue(), client);
    AtomicMessageQueue outgoing = attach_queue(id, client);
    AtomicMessageQueue incoming = attach_queue(id, server);
    ServiceQueueMessage message = ServiceQueueMessage__new(9999, 42);
    assert_push_error(&outgoing, &message, ATOMIC_MESSAGE_QUEUE_PENDING);
    accept_queue(id);
    for (uint64_t sequence = 0; sequence < 100; sequence++)
    {
        message.arg0 = sequence;
        assert_push_success(&outgoing, &message);
        match_result(
            AtomicMessageQueue__pop(&incoming),
            success(received)
            {
                TEST_ASSERT_EQUAL_UINT64(client->pid, received.sender_pid);
                TEST_ASSERT_EQUAL_UINT64(sequence, received.arg0);
                assert_push_success(&incoming, &received);
            },
            error_test_fail()
        );
        match_result(
            AtomicMessageQueue__pop(&outgoing),
            success(reply) { TEST_ASSERT_EQUAL_UINT64(server->pid, reply.sender_pid); },
            error_test_fail()
        );
    }
}

void test_full_and_corrupted_queue_do_not_affect_another_client(void)
{
    AtomicMessageQueueEndpointID endpoint = listen_queue();
    AtomicMessageQueueConnectionID id = connect_queue(endpoint, client);
    AtomicMessageQueueConnectionID second_id = connect_queue(endpoint, other);
    accept_queue(id);
    accept_queue(second_id);
    AtomicMessageQueue outgoing = attach_queue(id, client);
    AtomicMessageQueue incoming = attach_queue(id, server);
    AtomicMessageQueue second = attach_queue(second_id, other);
    AtomicMessageQueue second_server = attach_queue(second_id, server);
    ServiceQueueMessage message = ServiceQueueMessage__new(0, 1);
    for (uint64_t index = 0; index < ATOMIC_MESSAGE_QUEUE_CAPACITY; index++)
    {
        assert_push_success(&outgoing, &message);
    }
    assert_push_error(&outgoing, &message, ATOMIC_MESSAGE_QUEUE_FULL);
    match_result(AtomicMessageQueue__pop(&incoming),
        success_do_nothing(),
        error_test_fail()
    );
    assert_push_success(&outgoing, &message);
    __atomic_store_n(&outgoing.outgoing->produced, UINT64_MAX, __ATOMIC_RELEASE);
    match_result(
        AtomicMessageQueue__pop(&incoming),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_INVALID, code); }
    );
    __atomic_store_n(&outgoing.outgoing->consumed, UINT64_MAX, __ATOMIC_RELEASE);
    assert_push_error(&incoming, &message, ATOMIC_MESSAGE_QUEUE_INVALID);
    assert_push_success(&second, &message);
    match_result(AtomicMessageQueue__pop(&second_server),
        success_do_nothing(),
        error_test_fail()
    );
}

void test_close_retains_survivor_pages_until_its_acknowledgement(void)
{
    AtomicMessageQueueConnectionID id = connect_queue(listen_queue(), client);
    accept_queue(id);
    AtomicMessageQueue survivor = attach_queue(id, server);
    size_t before = KAllocStub__free_count();
    close_queue(id, client);
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_CLOSED, AtomicMessageQueue__status(&survivor));
    TEST_ASSERT_FALSE(mapped(client, AtomicMessageQueueLayout__address(id), false));
    TEST_ASSERT_TRUE(mapped(server, AtomicMessageQueueLayout__address(id), false));
    TEST_ASSERT_EQUAL_UINT64(before, KAllocStub__free_count());
    close_queue(id, server);
    TEST_ASSERT_EQUAL_UINT64(before + 1, KAllocStub__free_count());
    TEST_ASSERT_FALSE(mapped(server, AtomicMessageQueueLayout__address(id), false));
}

void test_close_commits_detachment_when_backing_free_fails_and_retries_later(void)
{
    AtomicMessageQueueEndpointID endpoint = listen_queue();
    AtomicMessageQueueConnectionID id = connect_queue(endpoint, client);
    close_queue(id, client);
    size_t before = KAllocStub__free_count();
    KAllocStub__set_free_failure(true);
    close_queue(id, server);
    TEST_ASSERT_FALSE(mapped(server, AtomicMessageQueueLayout__address(id), false));
    TEST_ASSERT_EQUAL_UINT64(before, KAllocStub__free_count());
    match_result(
        Scheduler__atomic_message_queue_operation(kernel->scheduler, server, SERVICE_ATOMIC_MESSAGE_QUEUE_CLOSE, id),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_DENIED, code); }
    );
    AtomicMessageQueueConnectionID other_id = connect_queue(endpoint, other);
    TEST_ASSERT_NOT_EQUAL(AtomicMessageQueueLayout__address(id), AtomicMessageQueueLayout__address(other_id));
    KAllocStub__set_free_failure(false);
    AtomicMessageQueueConnectionID replacement = connect_queue(endpoint, client);
    TEST_ASSERT_EQUAL_UINT64(before + 1, KAllocStub__free_count());
    TEST_ASSERT_NOT_EQUAL(id, replacement);
}

void test_exit_marks_peer_closed_and_removes_listener_without_losing_state(void)
{
    AtomicMessageQueueEndpointID endpoint = listen_queue();
    AtomicMessageQueueConnectionID id = connect_queue(endpoint, client);
    AtomicMessageQueue survivor = attach_queue(id, client);
    match_result(Scheduler__exit(kernel->scheduler, server, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_CLOSED, AtomicMessageQueue__status(&survivor));
    assert_endpoint_missing(&bstr("protected-test"));
    match_result(
        Scheduler__atomic_message_queue_operation(kernel->scheduler, other, SERVICE_ATOMIC_MESSAGE_QUEUE_CONNECT,
                                                  endpoint),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_INVALID, code); }
    );
    close_queue(id, client);
}

void test_signal_keeps_queue_mapped_until_owner_cpu_performs_teardown(void)
{
    AtomicMessageQueueEndpointID endpoint = listen_queue();
    AtomicMessageQueueConnectionID id = connect_queue(endpoint, client);
    accept_queue(id);
    AtomicMessageQueue survivor = attach_queue(id, client);
    ProcessID pid = server->pid;
    match_result(Scheduler__queue_signal(kernel->scheduler, pid, PROCESS_SIGNAL_KILL),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_OK, AtomicMessageQueue__status(&survivor));
    TEST_ASSERT_TRUE(mapped(server, AtomicMessageQueueLayout__address(id), false));
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
    TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_CLOSED, AtomicMessageQueue__status(&survivor));
    TEST_ASSERT_TRUE(mapped(client, AtomicMessageQueueLayout__address(id), false));
    assert_endpoint_missing(&bstr("protected-test"));
    size_t before = KAllocStub__free_count();
    close_queue(id, client);
    TEST_ASSERT_EQUAL_UINT64(before + 1, KAllocStub__free_count());
    TEST_ASSERT_FALSE(mapped(client, AtomicMessageQueueLayout__address(id), false));
}

void test_client_exit_before_accept_keeps_arrival_for_server_cleanup(void)
{
    AtomicMessageQueueConnectionID id = connect_queue(listen_queue(), client);
    match_result(Scheduler__exit(kernel->scheduler, client, 0),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(
        Scheduler__atomic_message_queue_operation(kernel->scheduler, server, SERVICE_ATOMIC_MESSAGE_QUEUE_ACCEPT, id),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_CLOSED, code); }
    );
    close_queue(id, server);
}

void test_only_owner_can_accept_or_close_and_stale_ids_cannot_close_reused_slot(void)
{
    AtomicMessageQueueEndpointID endpoint = listen_queue();
    AtomicMessageQueueConnectionID id = connect_queue(endpoint, client);
    match_result(
        Scheduler__atomic_message_queue_operation(kernel->scheduler, other, SERVICE_ATOMIC_MESSAGE_QUEUE_ACCEPT, id),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_DENIED, code); }
    );
    match_result(
        Scheduler__atomic_message_queue_operation(kernel->scheduler, other, SERVICE_ATOMIC_MESSAGE_QUEUE_CLOSE, id),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_DENIED, code); }
    );
    close_queue(id, server);
    close_queue(id, client);
    AtomicMessageQueueConnectionID replacement = connect_queue(endpoint, client);
    TEST_ASSERT_NOT_EQUAL(id, replacement);
    TEST_ASSERT_EQUAL_HEX64(AtomicMessageQueueLayout__address(id), AtomicMessageQueueLayout__address(replacement));
    match_result(
        Scheduler__atomic_message_queue_operation(kernel->scheduler, client, SERVICE_ATOMIC_MESSAGE_QUEUE_CLOSE, id),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_INVALID, code); }
    );
}

void test_listener_permission_duplicate_name_and_connection_admission(void)
{
    match_result(
        AtomicMessageQueues__listen(queues, client, &bstr("denied")),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_DENIED, code); }
    );
    AtomicMessageQueueEndpointID endpoint = listen_queue();
    other->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(other);
    match_result(
        AtomicMessageQueues__listen(queues, other, &bstr("protected-test")),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_DEVICE_ALREADY_REGISTERED, code); }
    );
    for (uint64_t index = 0; index < 4; index++)
    {
        connect_queue(endpoint, client);
    }
    match_result(
        Scheduler__atomic_message_queue_operation(kernel->scheduler, client, SERVICE_ATOMIC_MESSAGE_QUEUE_CONNECT,
                                                  endpoint),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_FULL, code); }
    );
    connect_queue(endpoint, other);
}

void test_allocation_failure_publishes_no_connection_and_invalid_name_pointer_is_rejected(void)
{
    AtomicMessageQueueEndpointID endpoint = listen_queue();
    KAllocStub__set_user_alloc_failure(true);
    match_result(
        Scheduler__atomic_message_queue_operation(kernel->scheduler, client, SERVICE_ATOMIC_MESSAGE_QUEUE_CONNECT,
                                                  endpoint),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_NO_MEMORY, code); }
    );
    TEST_ASSERT_EQUAL_UINT64(0, AtomicMessageQueueConnections__len(&queues->connections));
    Optional(ProcessPtr) caller = Some(ProcessPtr, server);
    match_result(
        Service__handler(kernel, &caller, SERVICE_ATOMIC_MESSAGE_QUEUE_LISTEN, UINT64_MAX, 10, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_INVALID, code); }
    );
}

void test_queue_syscalls_copy_tracked_name_and_bind_caller_identity(void)
{
    BString name = bstr("syscall-queue");
    UserAddress memory = NULL_USER_ADDRESS;
    match_result(
        Process__alloc_user_pages(server, HUND_SERVICE_PAGE_SIZE),
        success(address) { memory = address; },
        error_test_fail()
    );
    Kernel__memcopy((Address)BString__cstr(&name), memory.kernel_addr.virtual_addr, BString__length(&name));
    Optional(ProcessPtr) caller = Some(ProcessPtr, server);
    match_result(
        Service__handler(kernel, &caller, SERVICE_ATOMIC_MESSAGE_QUEUE_LISTEN, memory.virtual_addr,
                         BString__length(&name), 0, 0),
        success(address) { TEST_ASSERT_EQUAL_HEX64(ATOMIC_MESSAGE_QUEUE_LISTENER_BASE, address); },
        error_test_fail()
    );
    AtomicMessageQueueEndpointID endpoint = find_endpoint(&name);
    caller = Some(ProcessPtr, client);
    match_result(
        Service__handler(kernel, &caller, SERVICE_ATOMIC_MESSAGE_QUEUE_CONNECT, endpoint, 9999, 0, 0),
        success(id)
        {
            AtomicMessageQueue queue = attach_queue(id, client);
            TEST_ASSERT_EQUAL_UINT64(client->pid, queue.identity->client_pid);
            TEST_ASSERT_EQUAL_UINT64(endpoint, queue.identity->endpoint);
        },
        error_test_fail()
    );
}

void test_legacy_registration_cannot_replace_protected_listener(void)
{
    AtomicMessageQueueEndpointID endpoint = listen_queue();
    match_result(
        KernelServiceDirectory__request_queue(&kernel->service_directory, other, &bstr("protected-test"),
                                              SERVICE_KERNEL_EVENT_NONE),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_DEVICE_ALREADY_REGISTERED, code); }
    );
    TEST_ASSERT_EQUAL_UINT64(endpoint, find_endpoint(&bstr("protected-test")));
}

void test_arrivals_are_durable_and_backing_is_zeroed_before_publication(void)
{
    KAllocStub__set_alloc_fill(true, 0xa5);
    AtomicMessageQueueConnectionID id = connect_queue(listen_queue(), client);
    match_optional(
        AtomicMessageQueueListeners__ptr_at(&queues->listeners, 0),
        some(listener)
        {
            AtomicMessageQueueArrivalsPtr arrivals = k_v_ptr(listener->arrivals, AtomicMessageQueueArrivals);
            assert_arrival_connection(arrivals, id % ATOMIC_MESSAGE_QUEUE_LIMIT, id);
            AtomicMessageQueueEpoch epoch = AtomicMessageQueueArrivals__epoch(arrivals);
            close_queue(id, client);
            assert_arrival_connection(arrivals, id % ATOMIC_MESSAGE_QUEUE_LIMIT, id);
            TEST_ASSERT_TRUE(AtomicMessageQueueArrivals__epoch(arrivals) > epoch);
            close_queue(id, server);
            assert_arrival_connection(arrivals, id % ATOMIC_MESSAGE_QUEUE_LIMIT, 0);
        },
        none() { TEST_FAIL(); }
    );
    AtomicMessageQueueConnectionID replacement = connect_queue(find_endpoint(&bstr("protected-test")), client);
    AtomicMessageQueue queue = attach_queue(replacement, client);
    TEST_ASSERT_EQUAL_UINT64(0, queue.outgoing->produced);
    TEST_ASSERT_EQUAL_UINT64(0, queue.incoming->consumed);
    TEST_ASSERT_EQUAL_UINT64(0, queue.outgoing->messages[0].arg0);
    KAllocStub__set_alloc_fill(false, 0);
}

void test_arrival_iteration_keeps_connection_slots_stable_after_close(void)
{
    AtomicMessageQueueEndpointID endpoint = listen_queue();
    AtomicMessageQueueConnectionID first = connect_queue(endpoint, client);
    AtomicMessageQueueConnectionID second = connect_queue(endpoint, other);
    close_queue(first, client);
    close_queue(first, server);
    match_optional(
        AtomicMessageQueueListeners__ptr_at(&queues->listeners, 0),
        some(listener)
        {
            AtomicMessageQueueArrivalsPtr arrivals = k_v_ptr(listener->arrivals, AtomicMessageQueueArrivals);
            AtomicMessageQueueConnectionID occupied = 0;
            for_each_atomic_fa_ptr(AtomicMessageQueueConnectionSlots, &arrivals->connections, index, id, {
                if (id == 0)
                {
                    continue;
                }
                TEST_ASSERT_EQUAL_UINT64(second, id);
                TEST_ASSERT_EQUAL_UINT64(second % ATOMIC_MESSAGE_QUEUE_LIMIT, index);
                occupied++;
            });
            TEST_ASSERT_EQUAL_UINT64(1, occupied);
            assert_arrival_connection(arrivals, first % ATOMIC_MESSAGE_QUEUE_LIMIT, 0);
            assert_arrival_index_missing(arrivals, ATOMIC_MESSAGE_QUEUE_LIMIT);
        },
        none_test_fail()
    );
    close_queue(second, other);
    close_queue(second, server);
}
