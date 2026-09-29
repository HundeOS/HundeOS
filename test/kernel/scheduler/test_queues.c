#include "unity.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/syscall/service_handler.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/page_table.h"
#include "../stubs/kalloc.h"
#include "../stubs/panic.h"
#include "../stubs/process_context_layout.h"
#include "../stubs/cpu.h"
#include "../../../kernel/scheduler/shared_wait.h"
#include "../../../kernel/scheduler/preferred.h"
#include "ipc/queue_wait_control.h"

static KernelPtr kernel;
static ProcessPtr server;
static ProcessPtr client;
static ProcessPtr other;
static QueuesPtr queues;
extern uint64_t test_cpu_time_usec;

void setUp(void)
{
    test_cpu_time_usec = 0;
    reset_cpu_reschedule();
    KAllocStub__set_free_failure(false);
    clear_panic_state();
    KAllocStub__reset_allocation_tracking();
    kernel = init_test_kernel();
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
        some(endpoint)
        {
            return endpoint;
        },
        none_test_fail()
    );
    return 0;
}

static QueueWaitControlPtr shared_control(ProcessPtr process)
{
    match_result(Process__queue_wait_control(process),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(
        process->queue_wait_control,
        some(mapping)
        {
            return k_v_ptr(mapping, QueueWaitControl);
        },
        none_test_fail()
    );
    __builtin_trap();
}

static IpcDoorbellPtr process_doorbell(ProcessPtr process)
{
    match_result(
        Process__doorbell(process),
        success(mapping)
        {
            return k_v_ptr(mapping, IpcDoorbell);
        },
        error_test_fail()
    );
    __builtin_trap();
}

static void publish_shared_wait_until(QueueWaitControlPtr control, ConstQueueWaitSnapshotPtr observed,
                                      QueueWaitDeadline deadline)
{
    QueueWaitControl__begin(control);
    match_result(QueueWaitControl__add(control, observed),
        success_do_nothing(),
        error_test_fail()
    );
    QueueWaitControl__commit(control, test_cpu_time_usec, deadline, QueueWaitControl__wake_generation(control));
}

static void publish_shared_wait(QueueWaitControlPtr control, ConstQueueWaitSnapshotPtr observed)
{
    publish_shared_wait_until(control, observed, QUEUE_WAIT_NO_DEADLINE);
}

static QueueEndpointID listen_queue(void)
{
    match_result(
        Queues__listen(queues, server, &bstr("protected-test")),
        success(address)
        {
            TEST_ASSERT_EQUAL_HEX64(QUEUE_LISTENER_BASE, address);
        },
        error_test_fail()
    );
    return find_endpoint(&bstr("protected-test"));
}

static void assert_push_success(QueuePtr queue, ConstQueueMessagePtr message)
{
    match_result(
        Queue__push(queue, message),
        success(status)
        {
            TEST_ASSERT_EQUAL(QUEUE_OK, status);
        },
        error_test_fail()
    );
}

static void assert_push_error(QueuePtr queue, ConstQueueMessagePtr message, QueueCode expected)
{
    match_result(
        Queue__push(queue, message),
        success_test_fail(),
        error(code, error_message)
        {
            TEST_ASSERT_EQUAL(expected, code);
        }
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
        some(connection)
        {
            TEST_ASSERT_EQUAL_UINT64(expected, connection);
        },
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
        success(id)
        {
            return id;
        },
        error_test_fail()
    );
    return 0;
}

static UserAddress connection_memory(QueueConnectionID id)
{
    match_optional(
        QueueConnections__ptr_at(&queues->connections, id % QUEUE_LIMIT),
        some(connection)
        {
            return connection->memory;
        },
        none()
        {
            TEST_FAIL();
        }
    );
    return NULL_USER_ADDRESS;
}

static Queue attach_queue(QueueConnectionID id, ProcessPtr process)
{
    UserAddress memory = connection_memory(id);
    Address base = memory.kernel_addr.virtual_addr;
    QueueConnection connection =
        unwrap_optional(QueueConnections__at(&queues->connections, id % QUEUE_LIMIT), (QueueConnection){0});
    UserAddress peer_doorbell = process->pid == k_v_ptr(memory, QueueIdentity)->client_pid ? connection.server_doorbell
                                                                                           : connection.client_doorbell;
    match_result(
        Queue__attach(ptr(base, QueueIdentity), ptr((base + QUEUE_PAGE_SIZE), QueuePage),
                      ptr((base + 2 * QUEUE_PAGE_SIZE), QueuePage), id, process->pid,
                      k_v_ptr(peer_doorbell, IpcDoorbell), &Queue__ignore_data_available),
        success(queue)
        {
            return queue;
        },
        error_test_fail()
    );
    return (Queue){0};
}

static QueueWaitCode wait_snapshot(ProcessPtr process, ConstQueueWaitSnapshotPtr snapshot, Tick ticks)
{
    match_result(
        Process__alloc_user_pages(process, QUEUE_PAGE_SIZE),
        success(memory)
        {
            Kernel__memcopy((Address)snapshot, memory.kernel_addr.virtual_addr, sizeof(*snapshot));
            match_result(
                Scheduler__queue_wait(kernel->scheduler, process, memory.virtual_addr, 1, ticks),
                success(code)
                {
                    return code;
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
    return QUEUE_WAIT_TIMEOUT;
}

static QueueConnectionID accepted_connection(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    match_result(Queues__accept(queues, server, id),
        success_do_nothing(),
        error_test_fail()
    );
    return id;
}

void test_unread_messages_track_both_receivers_and_consumption(void)
{
    QueueConnectionID id = accepted_connection();
    Queue sender = attach_queue(id, client);
    Queue receiver = attach_queue(id, server);
    QueueMessage message = QueueMessage__new(0, 1);
    TEST_ASSERT_FALSE(Queues__has_unread_messages(queues, server));
    assert_push_success(&sender, &message);
    TEST_ASSERT_TRUE(Queues__has_unread_messages(queues, server));
    TEST_ASSERT_FALSE(Queues__has_unread_messages(queues, client));
    TEST_ASSERT_FALSE(Queues__has_unread_messages(queues, other));
    match_result(Queue__pop(&receiver),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_FALSE(Queues__has_unread_messages(queues, server));
    assert_push_success(&receiver, &message);
    TEST_ASSERT_TRUE(Queues__has_unread_messages(queues, client));
    TEST_ASSERT_FALSE(Queues__has_unread_messages(queues, server));
    match_result(Queues__close(queues, server, id),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_FALSE(Queues__has_unread_messages(queues, client));
    TEST_ASSERT_FALSE(Queues__has_unread_messages(queues, server));
}

void test_scheduler_retains_reply_processing_after_consumption_until_budget_expires(void)
{
    QueueConnectionID id = accepted_connection();
    Queue sender = attach_queue(id, server);
    Queue receiver = attach_queue(id, client);
    QueueMessage message = QueueMessage__new(0, 1);
    other->process_class = PROCESS_CLASS_REALTIME_SERVICE;
    client->last_tick = 100;
    assert_push_success(&sender, &message);
    ProcessContext context = {0};
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, client->state);
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, client->state);
    match_result(Queue__pop(&receiver),
        success_do_nothing(),
        error_test_fail()
    );
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, client->state);
    test_cpu_time_usec += SCHEDULER_PREFERRED_BUDGET_USEC;
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, other->state);
}

void test_scheduler_prioritises_request_receiver_on_yield(void)
{
    QueueConnectionID id = accepted_connection();
    Queue sender = attach_queue(id, client);
    QueueMessage message = QueueMessage__new(0, 1);
    other->process_class = PROCESS_CLASS_REALTIME_SERVICE;
    assert_push_success(&sender, &message);
    client->state = PROC_STATE_WAITING;
    Scheduler__yield_waiting(kernel->scheduler, client, 0);
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, server->state);
}

void test_scheduler_keeps_blocked_receiver_blocked(void)
{
    QueueConnectionID id = accepted_connection();
    Queue sender = attach_queue(id, client);
    QueueMessage message = QueueMessage__new(0, 1);
    assert_push_success(&sender, &message);
    server->state = PROC_STATE_WAITING;
    other->process_class = PROCESS_CLASS_REALTIME_SERVICE;
    ProcessContext context = {0};
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, server->state);
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, other->state);
}

void test_unchanged_shared_wait_is_respected_even_with_unread_messages(void)
{
    QueueConnectionID id = accepted_connection();
    Queue sender = attach_queue(id, client);
    Queue receiver = attach_queue(id, server);
    QueueMessage message = QueueMessage__new(0, 1);
    assert_push_success(&sender, &message);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    publish_shared_wait(shared_control(server), &observed);
    TEST_ASSERT_FALSE(Process__shared_wait_ready(server));
    other->process_class = PROCESS_CLASS_REALTIME_SERVICE;
    ProcessContext context = {0};
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, other->state);
}

static Queue publish_wait_with_unrelated_unread_message(void)
{
    QueueConnectionID first = accepted_connection();
    QueueConnectionID second = connect_queue(find_endpoint(&bstr("protected-test")), other);
    match_result(Queues__accept(queues, server, second),
        success_do_nothing(),
        error_test_fail()
    );
    Queue first_sender = attach_queue(first, client);
    Queue second_receiver = attach_queue(second, server);
    QueueMessage message = QueueMessage__new(0, 1);
    assert_push_success(&first_sender, &message);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&second_receiver);
    publish_shared_wait(shared_control(server), &observed);
    return attach_queue(second, other);
}

void test_unrelated_message_does_not_schedule_process_waiting_on_another_channel(void)
{
    Queue sender = publish_wait_with_unrelated_unread_message();
    other->process_class = PROCESS_CLASS_REALTIME_SERVICE;
    TEST_ASSERT_TRUE(Process__has_pending_messages(server));
    TEST_ASSERT_FALSE(Process__shared_wait_ready(server));
    ProcessContext context = {0};
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, other->state);

    QueueMessage message = QueueMessage__new(0, 1);
    assert_push_success(&sender, &message);
    TEST_ASSERT_TRUE(Process__shared_wait_ready(server));
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, server->state);
}

void test_current_receiver_releases_cpu_while_waiting_on_another_channel(void)
{
    Queue sender = publish_wait_with_unrelated_unread_message();
    server->state = PROC_STATE_RUNNING;
    other->process_class = PROCESS_CLASS_REALTIME_SERVICE;
    ProcessContext context = {0};
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, other->state);
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, server->state);

    QueueMessage message = QueueMessage__new(0, 1);
    assert_push_success(&sender, &message);
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, server->state);
}

void test_scheduler_rotates_between_queued_receivers_after_processing_budget(void)
{
    QueueConnectionID id = accepted_connection();
    Queue client_queue = attach_queue(id, client);
    Queue server_queue = attach_queue(id, server);
    QueueMessage message = QueueMessage__new(0, 1);
    assert_push_success(&client_queue, &message);
    assert_push_success(&server_queue, &message);
    server->last_tick = 0;
    client->last_tick = 100;
    other->process_class = PROCESS_CLASS_REALTIME_SERVICE;
    ProcessContext context = {0};
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, server->state);
    test_cpu_time_usec += SCHEDULER_PREFERRED_BUDGET_USEC;
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, client->state);
}

void test_scheduler_prioritises_pending_service_events(void)
{
    other->process_class = PROCESS_CLASS_REALTIME_SERVICE;
    match_result(ServiceEvents__push(&server->service.events, ServiceEvent__new(SERVICE_EVENT_IRQ, 42, 1)),
                 success_do_nothing(),
        error_test_fail()
    );
    ProcessContext context = {0};
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, server->state);
}

void test_queue_wait_detects_publication_between_snapshot_and_park(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    match_result(Queues__accept(queues, server, id),
        success_do_nothing(),
        error_test_fail()
    );
    Queue sender = attach_queue(id, client);
    Queue receiver = attach_queue(id, server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    QueueMessage message = {0};
    assert_push_success(&sender, &message);
    TEST_ASSERT_EQUAL(QUEUE_WAIT_CHANGED, wait_snapshot(server, &observed, 3));
    TEST_ASSERT_EQUAL_UINT64(0, QueueWaitUsage__snapshot(&server->queue_wait_usage).parks);
    TEST_ASSERT_EQUAL(PROC_STATE_READY, server->state);
    TEST_ASSERT_FALSE(Process__has_parked_operation(server, PARK_WAIT_QUEUE, &None(OperationKey)));
}

void test_shared_wait_tracks_publication_and_does_not_allocate_a_parked_operation(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    match_result(Queues__accept(queues, server, id),
        success_do_nothing(),
        error_test_fail()
    );
    Queue sender = attach_queue(id, client);
    Queue receiver = attach_queue(id, server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    QueueWaitControlPtr control = shared_control(server);
    test_cpu_time_usec = 1200000;
    publish_shared_wait(control, &observed);
    TEST_ASSERT_FALSE(Process__shared_wait_ready(server));
    test_cpu_time_usec = 2434567;
    ProcessRecord record = ProcessRecord__new(server, 0);
    match_optional(
        record.user_wait,
        some(user_wait)
        {
            TEST_ASSERT_EQUAL_UINT64(1200000, user_wait.started_at_usec);
            TEST_ASSERT_EQUAL_UINT64(1234567, user_wait.elapsed_usec);
        },
        none_test_fail()
    );
    ProcessInfo info = ProcessRecord__info(&record);
    TEST_ASSERT_EQUAL_STRING("USER_WAIT 1.234s", info.wait_reason);
    TEST_ASSERT_EQUAL_UINT64(1234567, info.user_wait_usec);
    TEST_ASSERT_EQUAL(PROC_STATE_READY, server->state);
    TEST_ASSERT_EQUAL_UINT64(0, ParkedOperations__len(&server->parked_operations));
    QueueMessage message = {0};
    assert_push_success(&sender, &message);
    TEST_ASSERT_TRUE(Process__shared_wait_ready(server));
    match_optional(Process__shared_wait(server),
        some_test_fail(),
        none_do_nothing()
    );
    ProcessRecord woken_record = ProcessRecord__new(server, 0);
    TEST_ASSERT_EQUAL_UINT64(1234567, woken_record.user_wait_usec);
    match_optional(woken_record.user_wait,
        some_test_fail(),
        none_do_nothing()
    );
    match_result(Queue__pop(&receiver),
        success_do_nothing(),
        error_test_fail()
    );
    // Clearing or immediately rearming cannot erase the queue's durable cursor.
    TEST_ASSERT_TRUE(Process__shared_wait_ready(server));
    observed = QueueWaitSnapshot__new_connection(&receiver);
    publish_shared_wait(control, &observed);
    TEST_ASSERT_FALSE(Process__shared_wait_ready(server));
    QueueWaitControl__clear(control);
    TEST_ASSERT_TRUE(Process__shared_wait_ready(server));
}

void test_shared_wait_releases_on_deadline_or_peer_exit_and_rejects_foreign_handles(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    Queue receiver = attach_queue(id, server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    QueueWaitControlPtr control = shared_control(server);
    publish_shared_wait(control, &observed);
    TEST_ASSERT_FALSE(Process__shared_wait_ready(server));
    test_cpu_time_usec = 10000;
    TEST_ASSERT_FALSE(Process__shared_wait_ready(server));
    test_cpu_time_usec = 0;
    publish_shared_wait_until(control, &observed, 10000);
    TEST_ASSERT_FALSE(Process__shared_wait_ready(server));
    test_cpu_time_usec = 10000;
    TEST_ASSERT_TRUE(Process__shared_wait_ready(server));
    publish_shared_wait(control, &observed);
    TEST_ASSERT_FALSE(Process__shared_wait_ready(server));
    QueueWaitControlPtr foreign = shared_control(other);
    publish_shared_wait(foreign, &observed);
    TEST_ASSERT_TRUE(Process__shared_wait_ready(other));
    match_result(Queues__close(queues, client, id),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(Process__shared_wait_ready(server));
}

void test_shared_wait_releases_on_kernel_wake_generation(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    Queue receiver = attach_queue(id, server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    publish_shared_wait(shared_control(server), &observed);
    TEST_ASSERT_FALSE(Process__shared_wait_ready(server));
    IpcDoorbellPtr doorbell = process_doorbell(server);
    IpcEventGeneration generation = IpcEvent__generation(IpcDoorbell__event(doorbell));

    Process__wake_shared_wait(server);

    TEST_ASSERT_EQUAL_UINT64(generation + 1, IpcEvent__generation(IpcDoorbell__event(doorbell)));
    TEST_ASSERT_TRUE(Process__shared_wait_ready(server));
}

void test_signal_wakes_process_from_shared_wait(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    Queue receiver = attach_queue(id, server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    QueueWaitControlPtr control = shared_control(server);
    publish_shared_wait(control, &observed);
    QueueWaitWakeGeneration generation = QueueWaitControl__wake_generation(control);
    TEST_ASSERT_FALSE(Process__shared_wait_ready(server));

    match_result(Scheduler__queue_signal(kernel->scheduler, server->pid, PROCESS_SIGNAL_INTERRUPT),
                 success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(generation + 1, QueueWaitControl__wake_generation(control));
    TEST_ASSERT_TRUE(Process__shared_wait_ready(server));
}

void test_child_exit_wakes_parent_from_shared_wait(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    Queue receiver = attach_queue(id, server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    QueueWaitControlPtr control = shared_control(server);
    publish_shared_wait(control, &observed);
    QueueWaitWakeGeneration generation = QueueWaitControl__wake_generation(control);
    KernelContext context = KernelContext__new(&Some(ProcessID, server->pid), &Some(SchedulerPtr, kernel->scheduler));
    match_result(Scheduler__park(kernel->scheduler, server, &context, &None(Continuation), PARK_WAIT_CHILD_EVENT),
                 success_do_nothing(),
        error_test_fail()
    );
    client->parent = Some(ProcessPtr, server);

    match_result(Scheduler__exit(kernel->scheduler, client, 0),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(generation + 1, QueueWaitControl__wake_generation(control));
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, server->state);
    TEST_ASSERT_TRUE(Process__shared_wait_ready(server));
}

void test_shared_wait_mapping_is_reused_and_cannot_be_freed_or_shared(void)
{
    QueueWaitControlPtr first = shared_control(server);
    TEST_ASSERT_EQUAL_PTR(first, shared_control(server));
    match_optional(
        server->queue_wait_control,
        some(mapping)
        {
            match_result(
                Process__free_user_pages(server, mapping.virtual_addr),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_STATIC_ALLOCATION, code);
                }
            );
            match_result(
                Process__map_shared_user_pages_with_allocation(other, server, mapping.virtual_addr, 4096),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_STATIC_ALLOCATION, code);
                }
            );
        },
        none_test_fail()
    );
}

void test_shared_wait_is_skipped_by_scheduler_and_service_events_override_it(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    Queue receiver = attach_queue(id, server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    publish_shared_wait(shared_control(server), &observed);
    ProcessContext context = {0};
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_NOT_EQUAL(PROC_STATE_RUNNING, server->state);
    TEST_ASSERT_TRUE(client->state == PROC_STATE_RUNNING || other->state == PROC_STATE_RUNNING);
    match_result(ServiceEvents__push(&server->service.events, ServiceEvent__new(SERVICE_EVENT_IRQ, 42, 1)),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(Process__shared_wait_ready(server));
}

void test_queue_wait_parks_and_timer_detects_peer_publication(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    match_result(Queues__accept(queues, server, id),
        success_do_nothing(),
        error_test_fail()
    );
    Queue sender = attach_queue(id, client);
    Queue receiver = attach_queue(id, server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    TEST_ASSERT_EQUAL(QUEUE_WAIT_PARKED, wait_snapshot(server, &observed, 3));
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, server->state);
    QueueMessage message = {0};
    assert_push_success(&sender, &message);
    ProcessContext context = {0};
    test_cpu_time_usec = 7000;
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    ProcessQueueWaitUsage usage = QueueWaitUsage__snapshot(&server->queue_wait_usage);
    TEST_ASSERT_EQUAL_UINT64(1, usage.parks);
    TEST_ASSERT_EQUAL_UINT64(1, usage.activity_wakes);
    TEST_ASSERT_EQUAL_UINT64(0, usage.timeout_wakes);
    TEST_ASSERT_EQUAL_UINT64(7000, usage.parked_usec);
    TEST_ASSERT_NOT_EQUAL(PROC_STATE_WAITING, server->state);
    TEST_ASSERT_FALSE(Queues__wait_changed(queues, server));
}

void test_queue_wait_on_another_cpu_detects_peer_publication_when_process_yields(void)
{
    ProcessPtr remote_server = dummy_process(40, 1, PROC_STATE_READY);
    remote_server->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(remote_server);
    match_result(Scheduler__schedule_on_cpu(kernel->scheduler, remote_server, 1),
        success_do_nothing(),
                 error_test_fail()
    );
    QueueEndpointID endpoint;
    match_result(
        Queues__listen(queues, remote_server, &bstr("remote-test")),
        success(address)
        {
            endpoint = find_endpoint(&bstr("remote-test"));
        },
        error_test_fail()
    );
    QueueConnectionID id = connect_queue(endpoint, client);
    match_result(Queues__accept(queues, remote_server, id),
        success_do_nothing(),
        error_test_fail()
    );
    Queue sender = attach_queue(id, client);
    Queue receiver = attach_queue(id, remote_server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    TEST_ASSERT_EQUAL(QUEUE_WAIT_PARKED, wait_snapshot(remote_server, &observed, QUEUE_WAIT_INDEFINITE_TICKS));
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, remote_server->state);

    QueueMessage message = {0};
    assert_push_success(&sender, &message);
    client->state = PROC_STATE_WAITING;
    test_cpu_time_usec = 7000;
    Scheduler__yield_waiting(kernel->scheduler, client, 0);

    ProcessQueueWaitUsage usage = QueueWaitUsage__snapshot(&remote_server->queue_wait_usage);
    TEST_ASSERT_NOT_EQUAL(PROC_STATE_WAITING, remote_server->state);
    TEST_ASSERT_EQUAL_UINT64(1, usage.activity_wakes);
    TEST_ASSERT_EQUAL_UINT64(7000, usage.parked_usec);
    TEST_ASSERT_FALSE(Queues__wait_changed(queues, remote_server));
}

void test_queue_notification_wakes_remote_waiter_and_coalesces_reschedule(void)
{
    ProcessPtr remote_server = dummy_process(40, 1, PROC_STATE_READY);
    remote_server->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(remote_server);
    match_result(Scheduler__schedule_on_cpu(kernel->scheduler, remote_server, 1),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Queues__listen(queues, remote_server, &bstr("notify-test")),
        success_do_nothing(),
        error_test_fail()
    );
    QueueConnectionID id = connect_queue(find_endpoint(&bstr("notify-test")), client);
    match_result(Queues__accept(queues, remote_server, id),
        success_do_nothing(),
        error_test_fail()
    );
    Queue sender = attach_queue(id, client);
    Queue receiver = attach_queue(id, remote_server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    TEST_ASSERT_EQUAL(QUEUE_WAIT_PARKED, wait_snapshot(remote_server, &observed, QUEUE_WAIT_INDEFINITE_TICKS));

    QueueMessage message = {0};
    assert_push_success(&sender, &message);
    test_cpu_time_usec = 7000;
    match_result(
        Scheduler__queue_data_available(kernel->scheduler, client, id),
        success(notification)
        {
            TEST_ASSERT_TRUE(notification.reschedule);
            TEST_ASSERT_EQUAL_UINT64(1, notification.cpu);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, remote_server->state);
    ProcessQueueWaitUsage usage = QueueWaitUsage__snapshot(&remote_server->queue_wait_usage);
    TEST_ASSERT_EQUAL_UINT64(1, usage.activity_wakes);
    TEST_ASSERT_EQUAL_UINT64(7000, usage.parked_usec);

    match_result(
        Scheduler__queue_data_available(kernel->scheduler, client, id),
        success(notification)
        {
            TEST_ASSERT_FALSE(notification.reschedule);
        },
        error_test_fail()
    );
    match_result(
        Scheduler__queue_data_available(kernel->scheduler, other, id),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_DENIED, code);
        }
    );
}

static void assert_notification_pokes_userspace_waiter(ProcessState state)
{
    ProcessPtr remote_server = dummy_process(40, 1, PROC_STATE_READY);
    remote_server->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(remote_server);
    match_result(Scheduler__schedule_on_cpu(kernel->scheduler, remote_server, 1),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Queues__listen(queues, remote_server, &bstr("userspace-notify-test")),
        success_do_nothing(),
                 error_test_fail()
    );
    QueueConnectionID id = connect_queue(find_endpoint(&bstr("userspace-notify-test")), client);
    match_result(Queues__accept(queues, remote_server, id),
        success_do_nothing(),
        error_test_fail()
    );
    Queue sender = attach_queue(id, client);
    Queue receiver = attach_queue(id, remote_server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    publish_shared_wait(shared_control(remote_server), &observed);
    remote_server->state = state;

    QueueMessage message = {0};
    assert_push_success(&sender, &message);
    match_result(
        Scheduler__queue_data_available(kernel->scheduler, client, id),
        success(notification)
        {
            TEST_ASSERT_TRUE(notification.reschedule);
            TEST_ASSERT_EQUAL_UINT64(1, notification.cpu);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(state, remote_server->state);

    match_result(
        Scheduler__queue_data_available(kernel->scheduler, client, id),
        success(notification)
        {
            TEST_ASSERT_FALSE(notification.reschedule);
        },
        error_test_fail()
    );
}

void test_queue_notification_pokes_interrupted_userspace_waiter(void)
{
    assert_notification_pokes_userspace_waiter(PROC_STATE_INTERRUPTED);
}

void test_queue_notification_does_not_interrupt_running_userspace_waiter(void)
{
    ProcessPtr remote_server = dummy_process(40, 1, PROC_STATE_READY);
    remote_server->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(remote_server);
    match_result(Scheduler__schedule_on_cpu(kernel->scheduler, remote_server, 1),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Queues__listen(queues, remote_server, &bstr("running-notify-test")),
        success_do_nothing(),
                 error_test_fail()
    );
    QueueConnectionID id = connect_queue(find_endpoint(&bstr("running-notify-test")), client);
    match_result(Queues__accept(queues, remote_server, id),
        success_do_nothing(),
        error_test_fail()
    );
    Queue sender = attach_queue(id, client);
    Queue receiver = attach_queue(id, remote_server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    publish_shared_wait(shared_control(remote_server), &observed);
    remote_server->state = PROC_STATE_RUNNING;

    QueueMessage message = {0};
    assert_push_success(&sender, &message);
    match_result(
        Scheduler__queue_data_available(kernel->scheduler, client, id),
        success(notification)
        {
            TEST_ASSERT_FALSE(notification.reschedule);
        },
        error_test_fail()
    );
}

void test_queue_wait_times_out_after_requested_ticks(void)
{
    match_result(
        Scheduler__queue_wait(kernel->scheduler, server, 0, 0, 3),
        success(code)
        {
            TEST_ASSERT_EQUAL(QUEUE_WAIT_PARKED, code);
        },
        error_test_fail()
    );
    ProcessContext context = {0};
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, server->state);
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, server->state);
    Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    TEST_ASSERT_NOT_EQUAL(PROC_STATE_WAITING, server->state);
}

void test_indefinite_queue_wait_does_not_expire_on_timer_interrupts(void)
{
    match_result(
        Scheduler__queue_wait(kernel->scheduler, server, 0, 0, QUEUE_WAIT_INDEFINITE_TICKS),
        success(code)
        {
            TEST_ASSERT_EQUAL(QUEUE_WAIT_PARKED, code);
        },
        error_test_fail()
    );
    ProcessContext context = {0};
    for (Tick tick = 0; tick < QUEUE_WAIT_MAX_TICKS + 1; tick++)
    {
        Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
    }
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, server->state);
    TEST_ASSERT_TRUE(Process__has_parked_operation(server, PARK_WAIT_QUEUE, &None(OperationKey)));
    ProcessQueueWaitUsage usage = QueueWaitUsage__snapshot(&server->queue_wait_usage);
    TEST_ASSERT_EQUAL_UINT64(0, usage.timeout_wakes);
}

void test_queue_wait_watches_consumption_acceptance_and_close(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    Queue sender = attach_queue(id, client);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&sender);
    match_result(Queues__watch(queues, client, &observed),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_FALSE(Queues__wait_changed(queues, client));
    match_result(Queues__accept(queues, server, id),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(Queues__wait_changed(queues, client));
    Queue receiver = attach_queue(id, server);
    QueueMessage message = {0};
    assert_push_success(&sender, &message);
    observed = QueueWaitSnapshot__new_connection(&sender);
    match_result(Queues__watch(queues, client, &observed),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_FALSE(Queues__wait_changed(queues, client));
    match_result(Queue__pop(&receiver),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(Queues__wait_changed(queues, client));
    observed = QueueWaitSnapshot__new_connection(&sender);
    match_result(Queues__watch(queues, client, &observed),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Queues__close(queues, server, id),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(Queues__wait_changed(queues, client));
}

void test_queue_wait_rejects_foreign_and_stale_handles(void)
{
    QueueConnectionID id = connect_queue(listen_queue(), client);
    Queue receiver = attach_queue(id, server);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(&receiver);
    match_result(
        Queues__watch(queues, other, &observed),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
    observed.handle += QUEUE_LIMIT;
    match_result(
        Queues__watch(queues, server, &observed),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
}

void test_queue_wait_validates_boundary_and_timeout(void)
{
    match_result(
        Scheduler__queue_wait(kernel->scheduler, server, UINT64_MAX, 1, 3),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
    match_result(
        Scheduler__queue_wait(kernel->scheduler, server, 0, QUEUE_WAIT_LIMIT + 1, 3),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
    match_result(
        Scheduler__queue_wait(kernel->scheduler, server, 0, 0, QUEUE_WAIT_MAX_TICKS + 1),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
    TEST_ASSERT_EQUAL(PROC_STATE_READY, server->state);
}

void test_queue_wait_syscall_returns_deferred_dispatch_marker(void)
{
    match_result(
        Service__handler(kernel, &Some(ProcessPtr, server), SERVICE_QUEUE_WAIT, 0, 0, 3, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSCALL_IO_BLOCK, code);
        }
    );
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, server->state);
    TEST_ASSERT_TRUE(Process__has_parked_operation(server, PARK_WAIT_QUEUE, &None(OperationKey)));
}

void test_pending_service_event_prevents_queue_park(void)
{
    match_result(Service__push_next_event(server, ServiceEvent__new(SERVICE_EVENT_MESSAGE, 1, 0)),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(
        Scheduler__queue_wait(kernel->scheduler, server, 0, 0, 3),
        success(code)
        {
            TEST_ASSERT_EQUAL(QUEUE_WAIT_CHANGED, code);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_READY, server->state);
}

void test_queue_timeout_has_distinct_completion_code(void)
{
    match_result(
        Scheduler__queue_wait(kernel->scheduler, server, 0, 0, 3),
        success(code)
        {
            TEST_ASSERT_EQUAL(QUEUE_WAIT_PARKED, code);
        },
        error_test_fail()
    );
    match_optional(
        CPUSchedulers__get(&kernel->scheduler->schedulers, 0),
        some(cpu)
        {
            TEST_ASSERT_FALSE(Process__wake_expired_sleep(server, cpu->last_tick + 2));
            test_cpu_time_usec = 50000;
            TEST_ASSERT_TRUE(Process__wake_expired_sleep(server, cpu->last_tick + 3));
        },
        none_test_fail()
    );
    ProcessQueueWaitUsage usage = QueueWaitUsage__snapshot(&server->queue_wait_usage);
    TEST_ASSERT_EQUAL_UINT64(1, usage.timeout_wakes);
    TEST_ASSERT_EQUAL_UINT64(0, usage.activity_wakes);
    TEST_ASSERT_EQUAL_UINT64(50000, usage.parked_usec);
    match_optional(
        Process__pop_ready_parked_operation(server),
        some(operation)
        {
            match_optional(
                operation->return_code,
                some(code)
                {
                    TEST_ASSERT_EQUAL(QUEUE_WAIT_TIMEOUT, code);
                },
                none_test_fail()
            );
            ParkedOperation__dispose(operation);
        },
        none_test_fail()
    );
}

void test_queue_wait_arrivals_are_owned_and_wake_on_connect(void)
{
    QueueEndpointID endpoint = listen_queue();
    match_optional(
        QueueListeners__at(&queues->listeners, endpoint % QUEUE_LISTENER_LIMIT),
        some(listener)
        {
            QueueWaitSnapshot observed = QueueWaitSnapshot__new_arrivals(k_v_ptr(listener.arrivals, QueueArrivals));
            match_result(
                Queues__watch(queues, client, &observed),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
                }
            );
            TEST_ASSERT_EQUAL(QUEUE_WAIT_PARKED, wait_snapshot(server, &observed, 3));
            connect_queue(endpoint, client);
            TEST_ASSERT_TRUE(Queues__wait_changed(queues, server));
            ProcessContext context = {0};
            Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
            TEST_ASSERT_NOT_EQUAL(PROC_STATE_WAITING, server->state);
        },
        none_test_fail()
    );
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

void test_connection_maps_peer_doorbells_and_unmaps_aliases_when_an_endpoint_closes(void)
{
    QueueEndpointID endpoint = listen_queue();
    QueueConnectionID id = connect_queue(endpoint, client);
    Address peer_alias = QueueLayout__peer_doorbell_address(id);

    TEST_ASSERT_TRUE(mapped(server, IPC_DOORBELL_ADDRESS, true));
    TEST_ASSERT_TRUE(mapped(client, IPC_DOORBELL_ADDRESS, true));
    TEST_ASSERT_TRUE(mapped(server, peer_alias, true));
    TEST_ASSERT_TRUE(mapped(client, peer_alias, true));
    TEST_ASSERT_FALSE(mapped(other, peer_alias, true));

    IpcDoorbellPtr client_event = process_doorbell(client);
    IpcEventGeneration accepted_generation = IpcEvent__generation(IpcDoorbell__event(client_event));
    accept_queue(id);
    TEST_ASSERT_EQUAL_UINT64(accepted_generation + 1, IpcEvent__generation(IpcDoorbell__event(client_event)));

    IpcDoorbellPtr server_event = process_doorbell(server);
    IpcEventGeneration closed_generation = IpcEvent__generation(IpcDoorbell__event(server_event));
    close_queue(id, client);
    TEST_ASSERT_TRUE(IpcEvent__changed(IpcDoorbell__event(server_event), closed_generation));
    TEST_ASSERT_FALSE(mapped(client, peer_alias, true));
    TEST_ASSERT_FALSE(mapped(server, peer_alias, true));
    TEST_ASSERT_TRUE(mapped(server, QueueLayout__address(id), false));

    close_queue(id, server);
}

void test_kernel_connection_maps_only_server_and_retains_backing_after_server_exit(void)
{
    Queue outgoing;
    match_result(
        Queues__connect_kernel(queues, listen_queue(), &Some(ProcessPtr, client)),
        success(queue)
        {
            outgoing = queue;
        },
        error_test_fail()
    );
    QueueConnectionID id = Queue__connection(&outgoing);
    match_optional(
        QueueConnections__at(&queues->connections, id % QUEUE_LIMIT),
        some(connection)
        {
            TEST_ASSERT_EQUAL_PTR(client, unwrap_optional(connection.kernel_origin, (ProcessPtr){}));
            TEST_ASSERT_EQUAL_UINT64(client->pid, Queue__origin_pid(&outgoing));
        },
        none_test_fail()
    );
    Address address = QueueLayout__address(id);
    TEST_ASSERT_TRUE(mapped(server, address, false));
    TEST_ASSERT_TRUE(mapped(server, address + QUEUE_PAGE_SIZE, false));
    TEST_ASSERT_TRUE(mapped(server, address + 2 * QUEUE_PAGE_SIZE, true));
    for (uint64_t page = 0; page < 3; page++)
    {
        TEST_ASSERT_FALSE(mapped(client, address + page * QUEUE_PAGE_SIZE, false));
        TEST_ASSERT_FALSE(mapped(client, address + page * QUEUE_PAGE_SIZE, true));
    }
    accept_queue(id);
    Queue incoming = attach_queue(id, server);
    QueueMessage request = QueueMessage__new(client->pid, 42);
    assert_push_success(&outgoing, &request);
    match_result(
        Queue__pop(&incoming),
        success(message)
        {
            TEST_ASSERT_EQUAL_UINT64(QUEUE_KERNEL_PID, message.sender_pid);
        },
        error_test_fail()
    );
    Queues__exit(queues, server);
    TEST_ASSERT_EQUAL(QUEUE_CLOSED, Queue__status(&outgoing));
    TEST_ASSERT_TRUE(connection_memory(id).valid);
    size_t freed = KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE);
    match_result(Queues__close_kernel(queues, id),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(freed + 1, KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE));
}

void test_kernel_close_waits_for_server_detachment_and_rejects_stale_handle(void)
{
    QueueEndpointID endpoint = listen_queue();
    Queue outgoing;
    match_result(
        Queues__connect_kernel(queues, endpoint, &None(ProcessPtr)),
        success(queue)
        {
            outgoing = queue;
        },
        error_test_fail()
    );
    QueueConnectionID id = Queue__connection(&outgoing);
    Queue incoming = attach_queue(id, server);
    size_t freed = KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE);
    match_result(Queues__close_kernel(queues, id),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(QUEUE_CLOSED, Queue__status(&incoming));
    TEST_ASSERT_EQUAL_UINT64(freed, KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE));
    close_queue(id, server);
    TEST_ASSERT_EQUAL_UINT64(freed + 1, KAllocStub__freed_allocation_count_for_size(QUEUE_CONNECTION_SIZE));
    match_result(
        Queues__connect_kernel(queues, endpoint, &None(ProcessPtr)),
        success(queue)
        {
            outgoing = queue;
        },
        error_test_fail()
    );
    TEST_ASSERT_NOT_EQUAL(id, Queue__connection(&outgoing));
    match_result(
        Queues__close_kernel(queues, id),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
    match_result(Queues__close_kernel(queues, Queue__connection(&outgoing)),
        success_do_nothing(),
        error_test_fail()
    );
    close_queue(Queue__connection(&outgoing), server);
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
            success(reply)
            {
                TEST_ASSERT_EQUAL_UINT64(server->pid, reply.sender_pid);
            },
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
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
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
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_DENIED, code);
        }
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
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
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
        Scheduler__preempt(kernel->scheduler, 0, &Some(ProcessContextPtr, &context));
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
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_CLOSED, code);
        }
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
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_DENIED, code);
        }
    );
    match_result(
        Scheduler__queue_operation(kernel->scheduler, other, SERVICE_QUEUE_CLOSE, id),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_DENIED, code);
        }
    );
    close_queue(id, server);
    close_queue(id, client);
    QueueConnectionID replacement = connect_queue(endpoint, client);
    TEST_ASSERT_NOT_EQUAL(id, replacement);
    TEST_ASSERT_EQUAL_HEX64(QueueLayout__address(id), QueueLayout__address(replacement));
    match_result(
        Scheduler__queue_operation(kernel->scheduler, client, SERVICE_QUEUE_CLOSE, id),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
}

void test_listener_permission_duplicate_name_and_connection_admission(void)
{
    match_result(
        Queues__listen(queues, client, &bstr("denied")),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_DENIED, code);
        }
    );
    QueueEndpointID endpoint = listen_queue();
    other->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(other);
    match_result(
        Queues__listen(queues, other, &bstr("protected-test")),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_DEVICE_ALREADY_REGISTERED, code);
        }
    );
    for (uint64_t index = 0; index < QUEUE_CLIENT_CONNECTION_LIMIT; index++)
    {
        connect_queue(endpoint, client);
    }
    match_result(
        Scheduler__queue_operation(kernel->scheduler, client, SERVICE_QUEUE_CONNECT, endpoint),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_FULL, code);
        }
    );
    connect_queue(endpoint, other);
}

void test_service_can_listen_on_multiple_named_endpoints(void)
{
    QueueEndpointID first = listen_queue();
    match_result(
        Queues__listen(queues, server, &bstr("second-protected-test")),
        success(address)
        {
            TEST_ASSERT_EQUAL_HEX64(QUEUE_LISTENER_BASE + QUEUE_ARRIVALS_SIZE, address);
        },
        error_test_fail()
    );

    QueueEndpointID second = find_endpoint(&bstr("second-protected-test"));
    TEST_ASSERT_NOT_EQUAL(first, second);
    connect_queue(first, client);
    connect_queue(second, other);
}

void test_allocation_failure_publishes_no_connection_and_invalid_name_pointer_is_rejected(void)
{
    QueueEndpointID endpoint = listen_queue();
    KAllocStub__set_user_alloc_failure(true);
    match_result(
        Scheduler__queue_operation(kernel->scheduler, client, SERVICE_QUEUE_CONNECT, endpoint),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_NO_MEMORY, code);
        }
    );
    TEST_ASSERT_EQUAL_UINT64(0, QueueConnections__len(&queues->connections));
    Optional(ProcessPtr) caller = Some(ProcessPtr, server);
    match_result(
        Service__handler(kernel, &caller, SERVICE_QUEUE_LISTEN, UINT64_MAX, 10, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
}

void test_shared_memory_manager_can_broker_more_than_ordinary_client_limit(void)
{
    QueueEndpointID endpoint = listen_queue();
    other->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(other);
    for (uint64_t index = 0; index < QUEUE_CLIENT_CONNECTION_LIMIT + 1; index++)
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
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_FULL, code);
        }
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
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_FULL, code);
        }
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
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_DENIED, code);
        }
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
        success(address)
        {
            memory = address;
        },
        error_test_fail()
    );
    Kernel__memcopy((Address)BString__cstr(&name), memory.kernel_addr.virtual_addr, BString__length(&name));
    Optional(ProcessPtr) caller = Some(ProcessPtr, server);
    match_result(
        Service__handler(kernel, &caller, SERVICE_QUEUE_LISTEN, memory.virtual_addr, BString__length(&name), 0, 0),
        success(address)
        {
            TEST_ASSERT_EQUAL_HEX64(QUEUE_LISTENER_BASE, address);
        },
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
        none()
        {
            TEST_FAIL();
        }
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
            for_each_atomic_fa_ptr(
                QueueConnectionSlots,
                &arrivals->connections,
                index,
                id,
                {
                    if (id == 0)
                    {
                        continue;
                    }
                    TEST_ASSERT_EQUAL_UINT64(second, id);
                    TEST_ASSERT_EQUAL_UINT64(second % QUEUE_LIMIT, index);
                    occupied++;
                }
            );
            TEST_ASSERT_EQUAL_UINT64(1, occupied);
            assert_arrival_connection(arrivals, first % QUEUE_LIMIT, 0);
            assert_arrival_index_missing(arrivals, QUEUE_LIMIT);
        },
        none_test_fail()
    );
    close_queue(second, other);
    close_queue(second, server);
}
