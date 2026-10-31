#include "unity.h"
#include "../../../user/lib/sys/queue_wait.h"
#include "../../../user/lib/sys/syscall.h"
#include "syscalls/service.h"
#include "ipc/ipc_platform.h"
#include "ipc/queue_wait_control.h"
#include <pthread.h>
#include <time.h>

static QueueIdentity identity;
static QueuePage outgoing;
static QueuePage incoming;
static IpcDoorbell peer_doorbell;
static Queue queue;
static QueueArrivals arrivals;
static unsigned waits;
static uint64_t expected_wait_ticks;
static unsigned registrations;
static QueueWaitControl control;
static SystemCallResult wait_result;
static bool make_readable_on_first_wait;
static unsigned cooperative_parks;
static unsigned cooperative_notifications;
static bool fail_cooperative_park;
static QueueWaitGate cooperative_gate = QUEUE_WAIT_GATE_INITIALIZER;

static Result(QueueWaitCode) cooperative_park(Anything context)
{
    cooperative_parks++;
    if (fail_cooperative_park)
    {
        return Error(QueueWaitCode, bstr("Cooperative wait interrupted"), QUEUE_DENIED);
    }
    if (context == 1)
    {
        if (cooperative_parks >= 2)
        {
            __atomic_store_n(&incoming.produced, 1, __ATOMIC_RELEASE);
        }
    }
    else if (context == 2)
    {
        __atomic_store_n(&incoming.consumed, 1, __ATOMIC_RELEASE);
    }
    else if (context == 3)
    {
        arrivals.epoch++;
    }
    else
    {
        QueueWaitGate__release(&cooperative_gate);
    }
    return Success(QueueWaitCode, QUEUE_WAIT_CHANGED);
}

static void cooperative_notify(Anything context)
{
    cooperative_notifications++;
}

SystemCallResult syscall(SystemCall number, Anything operation, Anything observed, Anything wait_ticks,
                         Anything unused1, Anything unused2)
{
    if (number == SERVICE_CALL && operation == SERVICE_QUEUE_WAIT_CONTROL)
    {
        registrations++;
        return (SystemCallResult)&control;
    }
    if (number == SERVICE_CALL && operation == SERVICE_BOOT_HARDWARE_INFO)
    {
        BootHardwareInfoPtr info = (BootHardwareInfoPtr)observed;
        *info = BootHardwareInfo__new();
        info->wait_mode = BOOT_WAIT_WFE;
        return SERVICE_OK;
    }
    if (number == SERVICE_CALL && operation == SERVICE_QUEUE_WAIT)
    {
        waits++;
        TEST_ASSERT_EQUAL_UINT64(expected_wait_ticks, wait_ticks);
        TEST_ASSERT_EQUAL_UINT64(0, unused1);
        TEST_ASSERT_EQUAL_UINT64(0, unused2);
        if (make_readable_on_first_wait && waits == 1)
        {
            __atomic_store_n(&incoming.produced, 1, __ATOMIC_RELEASE);
            return QUEUE_WAIT_CHANGED;
        }
        if (make_readable_on_first_wait)
        {
            return SYSCALL_INTERRUPTED;
        }
        return wait_result;
    }
    TEST_FAIL_MESSAGE("Unexpected syscall");
    return SERVICE_OPERATION_NOT_SUPPORTED;
}

static void delay(void)
{
    struct timespec duration = {.tv_nsec = 1000000};
    nanosleep(&duration, NULL);
}

static void * publish_readable_after_delay(void * context)
{
    delay();
    __atomic_store_n(&incoming.produced, 1, __ATOMIC_RELEASE);
    IpcPlatform__signal_event();
    return context;
}

static void * publish_writable_after_delay(void * context)
{
    delay();
    __atomic_store_n(&incoming.consumed, 1, __ATOMIC_RELEASE);
    IpcPlatform__signal_event();
    return context;
}

static void * publish_arrival_after_delay(void * context)
{
    delay();
    __atomic_store_n(&arrivals.epoch, 3, __ATOMIC_RELEASE);
    IpcPlatform__signal_event();
    return context;
}

void setUp(void)
{
    identity = (QueueIdentity){.connection = 123, .client_pid = 1, .server_pid = 2, .accepted = 1};
    outgoing = (QueuePage){0};
    incoming = (QueuePage){0};
    peer_doorbell = (IpcDoorbell){0};
    queue = Queue__new(&identity, &outgoing, &incoming, 123, 2, &peer_doorbell, &Queue__ignore_data_available);
    arrivals = (QueueArrivals){0};
    waits = 0;
    expected_wait_ticks = QUEUE_WAIT_INDEFINITE_TICKS;
    registrations = 0;
    control = (QueueWaitControl){0};
    wait_result = QUEUE_WAIT_TIMEOUT;
    make_readable_on_first_wait = false;
    cooperative_parks = cooperative_notifications = 0;
    fail_cooperative_park = false;
    cooperative_gate = (QueueWaitGate)QUEUE_WAIT_GATE_INITIALIZER;
    QueueWait__reset_after_fork();
}

void tearDown(void)
{
    QueueWait__swap_adapter(&None(QueueWaitAdapter));
    match_optional(QueueWaitControl__publication(&control),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_LESS_OR_EQUAL_UINT(1, registrations);
}

void test_prepare_uses_one_doorbell_generation_and_reuses_registration(void)
{
    Queue__prepare_wait();
    IpcPlatform__signal_event();
    Queue__wait_event();
    Queue__prepare_wait();
    IpcPlatform__signal_event();
    Queue__wait_event();

    TEST_ASSERT_EQUAL_UINT(1, registrations);
    TEST_ASSERT_EQUAL_UINT(0, waits);
}

void test_generic_wait_parks_until_the_process_doorbell_changes(void)
{
    Queue__prepare_wait();
    pthread_t peer;
    TEST_ASSERT_EQUAL(0, pthread_create(&peer, NULL, publish_readable_after_delay, NULL));
    Queue__wait_event();
    TEST_ASSERT_EQUAL(0, pthread_join(peer, NULL));
    TEST_ASSERT_GREATER_THAN(0, waits);
    TEST_ASSERT_EQUAL_UINT64(1, incoming.produced);
}

void test_timed_event_wait_passes_bounded_ticks_and_rearms_observation(void)
{
    expected_wait_ticks = 10;
    Queue__prepare_wait();
    Queue__wait_event_ticks(expected_wait_ticks);
    TEST_ASSERT_EQUAL_UINT(1, waits);
    TEST_ASSERT_EQUAL_UINT(1, registrations);
}

void test_readable_wait_checks_live_queue_state_before_parking(void)
{
    __atomic_store_n(&incoming.produced, 1, __ATOMIC_RELEASE);
    Queue__wait_readable(&queue);
    TEST_ASSERT_EQUAL_UINT(0, waits);
}

void test_readable_wait_parks_until_live_queue_state_changes(void)
{
    pthread_t peer;
    TEST_ASSERT_EQUAL(0, pthread_create(&peer, NULL, publish_readable_after_delay, NULL));
    Queue__wait_readable(&queue);
    TEST_ASSERT_EQUAL(0, pthread_join(peer, NULL));
    TEST_ASSERT_GREATER_THAN(0, waits);
}

void test_kernel_live_readiness_does_not_require_a_doorbell_change(void)
{
    make_readable_on_first_wait = true;
    IpcEventGeneration before = IpcEvent__generation(IpcDoorbell__event(IpcPlatform__local_doorbell()));

    TEST_ASSERT_TRUE(Queue__wait_readable_interruptible(&queue));
    TEST_ASSERT_EQUAL_UINT(1, waits);
    TEST_ASSERT_EQUAL_UINT64(before, IpcEvent__generation(IpcDoorbell__event(IpcPlatform__local_doorbell())));
}

void test_writable_wait_parks_until_peer_consumption_changes_the_doorbell(void)
{
    queue.written = QUEUE_CAPACITY;
    pthread_t peer;
    TEST_ASSERT_EQUAL(0, pthread_create(&peer, NULL, publish_writable_after_delay, NULL));
    Queue__wait_writable(&queue);
    TEST_ASSERT_EQUAL(0, pthread_join(peer, NULL));
    TEST_ASSERT_GREATER_THAN(0, waits);
}

void test_interruptible_queue_waits_report_signal_interruption(void)
{
    wait_result = SYSCALL_INTERRUPTED;

    TEST_ASSERT_FALSE(Queue__wait_readable_interruptible(&queue));
    queue.written = QUEUE_CAPACITY;
    TEST_ASSERT_FALSE(Queue__wait_writable_interruptible(&queue));
    TEST_ASSERT_EQUAL_UINT(2, waits);
}

void test_closed_queue_returns_without_parking(void)
{
    __atomic_store_n(&identity.closed, 1, __ATOMIC_RELEASE);
    Queue__wait_readable(&queue);
    Queue__wait_writable(&queue);
    TEST_ASSERT_EQUAL_UINT(0, waits);
}

void test_arrival_wait_observes_callers_epoch(void)
{
    arrivals.endpoint = 17;
    arrivals.epoch = 2;
    QueueArrivals__wait(&arrivals, 1);
    TEST_ASSERT_EQUAL_UINT(0, waits);

    pthread_t peer;
    TEST_ASSERT_EQUAL(0, pthread_create(&peer, NULL, publish_arrival_after_delay, NULL));
    QueueArrivals__wait(&arrivals, 2);
    TEST_ASSERT_EQUAL(0, pthread_join(peer, NULL));
    TEST_ASSERT_GREATER_THAN(0, waits);
}

void test_cooperative_read_wait_rechecks_after_spurious_wakes_without_kernel_wait(void)
{
    QueueWaitAdapter adapter = QueueWaitAdapter__new(1, cooperative_park, cooperative_notify);
    QueueWait__swap_adapter(&Some(QueueWaitAdapter, adapter));
    TEST_ASSERT_TRUE(Queue__wait_readable_interruptible(&queue));
    TEST_ASSERT_EQUAL_UINT(2, cooperative_parks);
    TEST_ASSERT_EQUAL_UINT(0, waits);
    TEST_ASSERT_EQUAL_UINT(0, registrations);
}

void test_cooperative_writable_and_arrival_waits_do_not_park_the_process(void)
{
    QueueWaitAdapter writable = QueueWaitAdapter__new(2, cooperative_park, cooperative_notify);
    QueueWait__swap_adapter(&Some(QueueWaitAdapter, writable));
    queue.written = QUEUE_CAPACITY;
    TEST_ASSERT_TRUE(Queue__wait_writable_interruptible(&queue));
    QueueWaitAdapter arrival = QueueWaitAdapter__new(3, cooperative_park, cooperative_notify);
    QueueWait__swap_adapter(&Some(QueueWaitAdapter, arrival));
    QueueArrivals__wait(&arrivals, 0);
    TEST_ASSERT_EQUAL_UINT(2, cooperative_parks);
    TEST_ASSERT_EQUAL_UINT(0, waits);
}

void test_cooperative_errors_propagate_and_fork_reset_restores_process_waits(void)
{
    QueueWaitAdapter adapter = QueueWaitAdapter__new(1, cooperative_park, cooperative_notify);
    QueueWait__swap_adapter(&Some(QueueWaitAdapter, adapter));
    fail_cooperative_park = true;
    TEST_ASSERT_FALSE(Queue__wait_readable_interruptible(&queue));
    TEST_ASSERT_EQUAL_UINT(0, waits);
    QueueWait__reset_after_fork();
    TEST_ASSERT_FALSE(QueueWait__has_adapter());
    wait_result = SYSCALL_INTERRUPTED;
    TEST_ASSERT_FALSE(Queue__wait_readable_interruptible(&queue));
    TEST_ASSERT_EQUAL_UINT(1, waits);
}

void test_cooperative_gate_parks_until_release_and_notifies_local_waiters(void)
{
    QueueWaitAdapter adapter = QueueWaitAdapter__new(4, cooperative_park, cooperative_notify);
    QueueWait__swap_adapter(&Some(QueueWaitAdapter, adapter));
    match_result(QueueWaitGate__acquire(&cooperative_gate),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(QueueWaitGate__acquire(&cooperative_gate),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT(1, cooperative_parks);
    TEST_ASSERT_EQUAL_UINT(1, cooperative_notifications);
    QueueWaitGate__release(&cooperative_gate);
    TEST_ASSERT_EQUAL_UINT(2, cooperative_notifications);
}

void test_gate_failure_keeps_ownership_and_unadapted_contention_is_an_error(void)
{
    match_result(QueueWaitGate__acquire(&cooperative_gate),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(
        QueueWaitGate__acquire(&cooperative_gate),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_INVALID, code);
        }
    );
    QueueWaitAdapter adapter = QueueWaitAdapter__new(4, cooperative_park, cooperative_notify);
    QueueWait__swap_adapter(&Some(QueueWaitAdapter, adapter));
    fail_cooperative_park = true;
    match_result(
        QueueWaitGate__acquire(&cooperative_gate),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(QUEUE_DENIED, code);
        }
    );
    QueueWaitGate__release(&cooperative_gate);
    match_result(QueueWaitGate__acquire(&cooperative_gate),
        success_do_nothing(),
        error_test_fail()
    );
    QueueWaitGate__release(&cooperative_gate);
}
