#include "unity.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/scheduler/preferred.h"
#include "../../../kernel/scheduler/shared_wait.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"
#include "../stubs/process_context_layout.h"
#include "../stubs/process.h"
#include "ipc/channel_protocol.h"
#include "ipc/queue_wait_control.h"

static KernelPtr kernel;
static SchedulerPtr scheduler;
static ProcessPtr p1;
static ProcessPtr p2;
static ProcessPtr p3;
static ProcessPtr relay;
extern uint64_t test_cpu_time_usec;

typedef struct TestRequest
{
    Queue caller;
    Queue forwarding;
    Queue responder;
    RequestTicket ticket;
} TestRequest;

static Queue attach(QueueConnectionID id, ProcessPtr process)
{
    QueueConnection connection =
        unwrap_optional(QueueConnections__at(&scheduler->queues.connections, id % QUEUE_LIMIT), (QueueConnection){0});
    Address base = connection.memory.kernel_addr.virtual_addr;
    return unwrap_result(Queue__attach((QueueIdentityPtr)base, (QueuePagePtr)(base + QUEUE_PAGE_SIZE),
                                       (QueuePagePtr)(base + 2 * QUEUE_PAGE_SIZE), id, process->pid),
                         (Queue){0});
}

static QueueConnectionID connect(ProcessPtr client, ProcessPtr server, ConstBStringPtr name)
{
    if (unwrap_optional(
            ServiceDirectory__find_endpoint(KernelServiceDirectory__kernel_directory(&kernel->service_directory), name),
            0) == 0)
    {
        match_result(Queues__listen(&scheduler->queues, server, name),
            success_do_nothing(),
            error_test_fail()
        );
    }
    QueueEndpointID endpoint = unwrap_optional(
        ServiceDirectory__find_endpoint(KernelServiceDirectory__kernel_directory(&kernel->service_directory), name), 0);
    QueueConnectionID id = unwrap_result(Queues__connect(&scheduler->queues, client, endpoint), 0);
    TEST_ASSERT_NOT_EQUAL(0, id);
    match_result(Queues__accept(&scheduler->queues, server, id),
        success_do_nothing(),
        error_test_fail()
    );
    return id;
}

static void push(QueuePtr queue, ConstQueueMessagePtr message)
{
    match_result(Queue__push(queue, message),
        success_do_nothing(),
        error_test_fail()
    );
}

static TestRequest request(ProcessPtr caller, ProcessPtr responder, ConstBStringPtr first, ConstBStringPtr second,
                           const OptionalPtr(RequestTicket) parent)
{
    QueueConnectionID source = connect(caller, relay, first);
    QueueConnectionID route = connect(relay, responder, second);
    TestRequest result = {.caller = attach(source, caller),
                          .forwarding = attach(route, relay),
                          .responder = attach(route, responder),
                          .ticket = RequestTicket__new(source, 1, caller->pid, parent)};
    QueueMessage submit = ChannelControlMessage__new(CHANNEL_REQUEST_SUBMIT, 0, 0, source);
    submit.request = result.ticket.metadata;
    push(&result.caller, &submit);
    QueueMessage serve = ChannelControlMessage__new(CHANNEL_REQUEST_SERVE, 0, 0, source);
    serve.request = result.ticket.metadata;
    serve.arg3 = caller->pid;
    push(&result.forwarding, &serve);
    match_result(Queue__pop(&result.responder),
        success_do_nothing(),
        error_test_fail()
    );
    return result;
}

static void reply(TestRequest * request)
{
    QueueMessage message = ChannelControlMessage__new(CHANNEL_RESPONSE_REPLIED, 0, 0, request->ticket.connection);
    message.request = request->ticket.metadata;
    push(&request->responder, &message);
}

static void wait_on(ProcessPtr process, QueuePtr queue)
{
    match_result(Process__queue_wait_control(process),
        success_do_nothing(),
        error_test_fail()
    );
    UserAddress mapping = unwrap_optional(process->queue_wait_control, (UserAddress){0});
    QueueWaitControlPtr control = k_v_ptr(mapping, QueueWaitControl);
    QueueWaitSnapshot observed = QueueWaitSnapshot__new_connection(queue);
    QueueWaitControl__begin(control);
    match_result(QueueWaitControl__add(control, &observed),
        success_do_nothing(),
        error_test_fail()
    );
    QueueWaitControl__commit(control, test_cpu_time_usec, QUEUE_WAIT_NO_DEADLINE,
                             QueueWaitControl__wake_generation(control));
}

static CPUSchedulerPtr cpu(CPUID id)
{
    return unwrap_optional(CPUSchedulers__at(&scheduler->schedulers, id), (CPUSchedulerPtr){});
}

void setUp(void)
{
    clear_panic_state();
    test_cpu_time_usec = 0;
    kernel = init_test_kernel();
    scheduler = kernel->scheduler;
    p1 = dummy_process(1, 0, PROC_STATE_READY);
    p2 = dummy_process(2, 0, PROC_STATE_READY);
    p3 = dummy_process(3, 0, PROC_STATE_READY);
    relay = dummy_process(4, 0, PROC_STATE_READY);
    Process__set_hund_identity(p1);
    Process__set_hund_identity(p2);
    Process__set_hund_identity(p3);
    Process__set_hund_identity(relay);
    relay->process_class = PROCESS_CLASS_SERVICE;
    p2->process_class = PROCESS_CLASS_SERVICE;
    p3->process_class = PROCESS_CLASS_SERVICE;
    match_result(Scheduler__schedule_on_cpu(scheduler, p1, 0),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, p2, 0),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, p3, 0),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, relay, 0),
        success_do_nothing(),
        error_test_fail()
    );
    cpu(0)->started = true;
    cpu(1)->started = true;
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_preemption_selects_under_lock_and_switches_after_unlock(void)
{
    current_process_reset();
    p1->state = PROC_STATE_RUNNING;
    ProcessContext context = {0};
    Scheduler__preempt(scheduler, 0, &context);
    TEST_ASSERT_TRUE(context_saved_with_scheduler_lock());
    TEST_ASSERT_NOT_NULL(current_running_process());
    TEST_ASSERT_NOT_EQUAL(p1, current_running_process());
    TEST_ASSERT_FALSE(context_switched_with_scheduler_lock());
    TEST_ASSERT_TRUE(mutex_trylock(&scheduler->scheduler_lock));
    mutex_unlock(&scheduler->scheduler_lock);
}

void test_graph_authenticates_nested_requests_and_origin(void)
{
    TestRequest root = request(p1, p2, &bstr("a"), &bstr("b"), &None(RequestTicket));
    TestRequest child = request(p2, p3, &bstr("a"), &bstr("d"), &Some(RequestTicket, root.ticket));
    RequestGraph__refresh(&scheduler->request_graph, &scheduler->queues);
    RequestLink link =
        unwrap_optional(RequestGraph__find(&scheduler->request_graph, child.ticket.connection, 1), (RequestLink){0});
    TEST_ASSERT_EQUAL_PTR(p2, link.caller);
    TEST_ASSERT_EQUAL_PTR(p3, link.responder);
    TEST_ASSERT_TRUE(RequestGraph__valid(&scheduler->request_graph, &link));
    child.ticket.metadata.origin_pid++;
    link.ticket = child.ticket;
    TEST_ASSERT_FALSE(RequestGraph__valid(&scheduler->request_graph, &link));
    link.ticket = root.ticket;
    link.ticket.metadata.origin_pid++;
    TEST_ASSERT_FALSE(RequestGraph__valid(&scheduler->request_graph, &link));
}

void test_reply_queues_caller_and_warm_ancestor_without_preemption(void)
{
    TestRequest root = request(p1, p2, &bstr("a"), &bstr("b"), &None(RequestTicket));
    TestRequest child = request(p2, p3, &bstr("a"), &bstr("d"), &Some(RequestTicket, root.ticket));
    wait_on(p1, &root.caller);
    wait_on(p2, &child.caller);
    reply(&child);
    p3->state = PROC_STATE_RUNNING;
    Scheduler__refresh_preferred(scheduler);
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, p3->state);
    TEST_ASSERT_EQUAL(PROC_STATE_READY, p1->state);
    TEST_ASSERT_TRUE(p1->preferred_warm);
    TEST_ASSERT_TRUE(p2->preferred_warm);
    ProcessPtr first = unwrap_optional(Scheduler__take_preferred(scheduler, cpu(1)), (ProcessPtr){});
    TEST_ASSERT_EQUAL_PTR(relay, first);
    first->state = PROC_STATE_RUNNING;
    ProcessPtr second = unwrap_optional(Scheduler__take_preferred(scheduler, cpu(0)), (ProcessPtr){});
    TEST_ASSERT_EQUAL_PTR(p2, second);
    second->state = PROC_STATE_RUNNING;
    ProcessPtr third = unwrap_optional(Scheduler__take_preferred(scheduler, cpu(0)), (ProcessPtr){});
    TEST_ASSERT_EQUAL_PTR(p1, third);
}

void test_residency_starts_on_dispatch_and_does_not_reset_on_timer_checks(void)
{
    TestRequest root = request(p1, p2, &bstr("a"), &bstr("b"), &None(RequestTicket));
    wait_on(p1, &root.caller);
    reply(&root);
    Scheduler__refresh_preferred(scheduler);
    Scheduler__forget_preferred(scheduler, relay);
    test_cpu_time_usec = 5000;
    TEST_ASSERT_EQUAL_PTR(p1, unwrap_optional(Scheduler__take_preferred(scheduler, cpu(1)), (ProcessPtr){}));
    ContextSwitch selected = ContextSwitch__new(p1, &None(ProcessPtr));
    ContextSwitch__switch(&selected);
    TEST_ASSERT_EQUAL_UINT64(5000, p1->dispatched_at);
    test_cpu_time_usec = 5000 + SCHEDULER_WARM_RESIDENCY_USEC - 1;
    TEST_ASSERT_TRUE(Process__retain_preferred(p1));
    TEST_ASSERT_EQUAL_UINT64(5000, p1->dispatched_at);
    test_cpu_time_usec = 5000 + SCHEDULER_WARM_RESIDENCY_USEC;
    TEST_ASSERT_FALSE(Process__retain_preferred(p1));
}

void test_preferred_selection_does_not_migrate_unfinished_switch_or_kernel_wait(void)
{
    TestRequest root = request(p1, p2, &bstr("a"), &bstr("b"), &None(RequestTicket));
    wait_on(p1, &root.caller);
    reply(&root);
    Scheduler__refresh_preferred(scheduler);
    Scheduler__forget_preferred(scheduler, relay);
    p1->migration_safe = false;
    match_optional(Scheduler__take_preferred(scheduler, cpu(1)),
        some_test_fail(),
        none_do_nothing()
    );
    p1->migration_safe = true;
    p1->state = PROC_STATE_WAITING;
    match_optional(Scheduler__take_preferred(scheduler, cpu(1)),
        some_test_fail(),
        none_do_nothing()
    );
    p1->state = PROC_STATE_INTERRUPTED;
    TEST_ASSERT_EQUAL_PTR(p1, unwrap_optional(Scheduler__take_preferred(scheduler, cpu(1)), (ProcessPtr){}));
    match_optional(ProcessTable__find_item(&cpu(0)->processes, p1),
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(ProcessTable__find_item(&cpu(1)->processes, p1),
        some_do_nothing(),
        none_test_fail()
    );
}

void test_ready_exec_continuation_preempts_an_active_preferred_process(void)
{
    KernelContext context = KernelContext__new(&None(ProcessID), &None(SchedulerPtr));
    ParkedOperationPtr operation = ParkedOperation__heap_new(&context, &None(Continuation), PARK_WAIT_EXEC_LOAD);
    match_result(Process__push_parked_operation(p1, operation),
        success_do_nothing(),
        error_test_fail()
    );
    ParkedOperation__mark_ready(operation);
    p1->state = PROC_STATE_INTERRUPTED;
    p2->state = PROC_STATE_RUNNING;
    p2->preferred_active = true;

    current_process_reset();
    Scheduler__preempt(scheduler, 0, NULL); // hundelint: ignore-no-null

    TEST_ASSERT_EQUAL_PTR(p1, current_running_process());
}

void test_acknowledged_ticket_withdraws_speculation_and_cannot_be_reused(void)
{
    TestRequest root = request(p1, p2, &bstr("a"), &bstr("b"), &None(RequestTicket));
    wait_on(p1, &root.caller);
    reply(&root);
    Scheduler__refresh_preferred(scheduler);
    QueueMessage ack = ChannelControlMessage__new(CHANNEL_REQUEST_ACKNOWLEDGE, 0, 0, root.ticket.connection);
    ack.request = root.ticket.metadata;
    push(&root.caller, &ack);
    Scheduler__refresh_preferred(scheduler);
    match_optional(ProcessTable__find_item(&scheduler->preferred, p1),
        some_test_fail(),
        none_do_nothing()
    );
    root.ticket.metadata.generation++;
    QueueMessage submit = ChannelControlMessage__new(CHANNEL_REQUEST_SUBMIT, 0, 0, root.ticket.connection);
    submit.request = root.ticket.metadata;
    push(&root.caller, &submit);
    RequestGraph__refresh(&scheduler->request_graph, &scheduler->queues);
    TEST_ASSERT_EQUAL_UINT64(0, RequestLinks__len(&scheduler->request_graph.links));
}

void test_foreign_relay_cannot_claim_another_clients_ticket(void)
{
    TestRequest root = request(p1, p2, &bstr("a"), &bstr("b"), &None(RequestTicket));
    QueueConnectionID id = connect(p2, p3, &bstr("foreign"));
    Queue forged = attach(id, p2);
    QueueMessage serve = ChannelControlMessage__new(CHANNEL_REQUEST_SERVE, 0, 0, root.ticket.connection);
    serve.request = root.ticket.metadata;
    serve.arg3 = p1->pid;
    push(&forged, &serve);
    RequestGraph__refresh(&scheduler->request_graph, &scheduler->queues);
    TEST_ASSERT_EQUAL_UINT64(1, RequestLinks__len(&scheduler->request_graph.links));
    RequestLink link =
        unwrap_optional(RequestGraph__find(&scheduler->request_graph, root.ticket.connection, 1), (RequestLink){0});
    TEST_ASSERT_EQUAL_PTR(p2, link.responder);
}

void test_parent_generation_and_handler_must_match(void)
{
    TestRequest root = request(p1, p2, &bstr("a"), &bstr("b"), &None(RequestTicket));
    TestRequest child = request(p3, p2, &bstr("a"), &bstr("b"), &Some(RequestTicket, root.ticket));
    RequestGraph__refresh(&scheduler->request_graph, &scheduler->queues);
    RequestLink link =
        unwrap_optional(RequestGraph__find(&scheduler->request_graph, child.ticket.connection, 1), (RequestLink){0});
    TEST_ASSERT_FALSE(RequestGraph__valid(&scheduler->request_graph, &link));
    link.caller = p2;
    link.ticket.metadata.parent_generation++;
    TEST_ASSERT_FALSE(RequestGraph__valid(&scheduler->request_graph, &link));
}

void test_ancestry_cycles_are_bounded_and_rejected(void)
{
    p1->process_class = PROCESS_CLASS_SERVICE;
    TestRequest root = request(p1, p2, &bstr("a"), &bstr("b"), &None(RequestTicket));
    TestRequest child = request(p2, p1, &bstr("a"), &bstr("c"), &Some(RequestTicket, root.ticket));
    root.ticket.metadata.parent_connection = child.ticket.connection;
    root.ticket.metadata.parent_generation = child.ticket.metadata.generation;
    QueueMessage submit = ChannelControlMessage__new(CHANNEL_REQUEST_SUBMIT, 0, 0, root.ticket.connection);
    submit.request = root.ticket.metadata;
    push(&root.caller, &submit);
    submit.kind = CHANNEL_REQUEST_SERVE;
    submit.arg3 = p1->pid;
    push(&root.forwarding, &submit);
    RequestGraph__refresh(&scheduler->request_graph, &scheduler->queues);
    RequestLink link =
        unwrap_optional(RequestGraph__find(&scheduler->request_graph, root.ticket.connection, 1), (RequestLink){0});
    TEST_ASSERT_FALSE(RequestGraph__valid(&scheduler->request_graph, &link));
}

void test_process_exit_removes_queued_work_before_disposal(void)
{
    TestRequest root = request(p1, p2, &bstr("a"), &bstr("b"), &None(RequestTicket));
    wait_on(p1, &root.caller);
    reply(&root);
    Scheduler__refresh_preferred(scheduler);
    match_result(Scheduler__exit(scheduler, p1, 0),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(ProcessTable__find_item(&scheduler->preferred, p1),
        some_test_fail(),
        none_do_nothing()
    );
    RequestGraph__refresh(&scheduler->request_graph, &scheduler->queues);
    TEST_ASSERT_EQUAL_UINT64(0, RequestLinks__len(&scheduler->request_graph.links));
}

void test_preferred_processing_yields_on_shared_wait_before_budget(void)
{
    TestRequest root = request(p1, p2, &bstr("a"), &bstr("b"), &None(RequestTicket));
    Scheduler__refresh_preferred(scheduler);
    Scheduler__forget_preferred(scheduler, relay);
    TEST_ASSERT_EQUAL_PTR(p2, unwrap_optional(Scheduler__take_preferred(scheduler, cpu(0)), (ProcessPtr){}));
    p2->dispatched_at = 0;
    test_cpu_time_usec = 100;
    TEST_ASSERT_TRUE(Process__retain_preferred(p2));
    wait_on(p2, &root.responder);
    TEST_ASSERT_FALSE(Process__retain_preferred(p2));
}
