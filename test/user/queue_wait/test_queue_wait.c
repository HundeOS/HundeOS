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
static unsigned registrations;
static QueueWaitControl control;
static SystemCallResult wait_result;

SystemCallResult syscall(SystemCall number, Anything operation, Anything address, Anything count, Anything ticks,
                         Anything unused)
{
    if (number == SERVICE_CALL && operation == SERVICE_QUEUE_WAIT_CONTROL)
    {
        registrations++;
        return (SystemCallResult)&control;
    }
    if (number == SERVICE_CALL && operation == SERVICE_BOOT_HARDWARE_INFO)
    {
        BootHardwareInfoPtr info = (BootHardwareInfoPtr)address;
        *info = BootHardwareInfo__new();
        info->wait_mode = BOOT_WAIT_WFE;
        return SERVICE_OK;
    }
    if (number == SERVICE_CALL && operation == SERVICE_QUEUE_WAIT)
    {
        waits++;
        TEST_ASSERT_NOT_EQUAL(0, address);
        TEST_ASSERT_GREATER_THAN(0, count);
        TEST_ASSERT_EQUAL_UINT64(QUEUE_WAIT_INDEFINITE_TICKS, ticks);
        return wait_result;
    }
    TEST_FAIL_MESSAGE("Unexpected syscall");
    return SERVICE_OPERATION_NOT_SUPPORTED;
}

static void * publish_after_delay(void * context)
{
    struct timespec delay = {.tv_nsec = 1000000};
    nanosleep(&delay, NULL);
    __atomic_store_n(&incoming.produced, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&incoming.consumed, 1, __ATOMIC_RELEASE);
    IpcPlatform__signal_event();
    return context;
}

static void * signal_after_delay(void * context)
{
    struct timespec delay = {.tv_nsec = 1000000};
    nanosleep(&delay, NULL);
    IpcPlatform__signal_event();
    return context;
}

static void * signal_then_publish_after_delay(void * context)
{
    signal_after_delay(context);
    signal_after_delay(context);
    return publish_after_delay(context);
}

static void * wake_after_delay(void * context)
{
    struct timespec delay = {.tv_nsec = 1000000};
    nanosleep(&delay, NULL);
    QueueWaitControl__wake(&control);
    IpcPlatform__signal_event();
    return context;
}

static void * publish_arrival_after_delay(void * context)
{
    struct timespec delay = {.tv_nsec = 1000000};
    nanosleep(&delay, NULL);
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
    waits = 0;
    wait_result = QUEUE_WAIT_TIMEOUT;
    ConstIpcEventPtr event = IpcDoorbell__event(IpcPlatform__local_doorbell());
    IpcEventGeneration observed = IpcPlatform__prepare_wait(event);
    IpcPlatform__signal_event();
    IpcPlatform__wait_event(event, observed);
    match_result(QueueWait__remember_queue(&queue),
        success_do_nothing(),
        error_test_fail()
    );
}

void tearDown(void)
{
    match_optional(QueueWaitControl__publication(&control),
        some_test_fail(),
        none_do_nothing()
    );
    QueueWait__forget(123);
    TEST_ASSERT_EQUAL(1, registrations);
}

void test_idle_wait_ignores_unrelated_event_until_watched_queue_changes(void)
{
    IODiagnosticsSnapshot before = IODiagnostics__snapshot(&control.diagnostics);
    QueueWaitCompletedUsec wait_before = QueueWaitControl__completed_wait_usec(&control);
    Queue__prepare_wait();
    pthread_t peer;
    TEST_ASSERT_EQUAL(0, pthread_create(&peer, NULL, signal_then_publish_after_delay, NULL));
    Queue__wait_event();
    TEST_ASSERT_EQUAL(0, pthread_join(peer, NULL));
    TEST_ASSERT_EQUAL_UINT64(1, incoming.produced);
    TEST_ASSERT_GREATER_THAN(0, waits);
    IODiagnosticsSnapshot after = IODiagnostics__snapshot(&control.diagnostics);
    TEST_ASSERT_EQUAL_UINT64(before.wait_entries + 1, after.wait_entries);
    TEST_ASSERT_GREATER_THAN_UINT64(before.unchanged_wakes, after.unchanged_wakes);
    TEST_ASSERT_GREATER_THAN_UINT64(wait_before, QueueWaitControl__completed_wait_usec(&control));
}

void test_event_before_wait_does_not_end_wait_without_authoritative_change(void)
{
    Queue__prepare_wait();
    IpcPlatform__signal_event();
    pthread_t peer;
    TEST_ASSERT_EQUAL(0, pthread_create(&peer, NULL, publish_after_delay, NULL));
    Queue__wait_event();
    TEST_ASSERT_EQUAL(0, pthread_join(peer, NULL));
    TEST_ASSERT_GREATER_THAN(0, waits);
}

void test_kernel_wake_generation_returns_to_dispatch_without_queue_change(void)
{
    Queue__prepare_wait();
    pthread_t peer;
    TEST_ASSERT_EQUAL(0, pthread_create(&peer, NULL, wake_after_delay, NULL));
    Queue__wait_event();
    TEST_ASSERT_EQUAL(0, pthread_join(peer, NULL));
    TEST_ASSERT_EQUAL_UINT64(0, incoming.produced);
    TEST_ASSERT_GREATER_THAN(0, waits);
}

void test_activity_between_prepare_and_wait_stays_in_userspace(void)
{
    Queue__prepare_wait();
    __atomic_store_n(&incoming.produced, 1, __ATOMIC_RELEASE);
    Queue__wait_event();
    TEST_ASSERT_EQUAL(0, waits);
}

void test_peer_closure_stays_in_userspace(void)
{
    Queue__prepare_wait();
    __atomic_store_n(&identity.closed, 1, __ATOMIC_RELEASE);
    Queue__wait_event();
    TEST_ASSERT_EQUAL(0, waits);
}

void test_consumption_between_prepare_and_wait_stays_in_userspace(void)
{
    Queue__prepare_wait();
    __atomic_store_n(&incoming.consumed, 1, __ATOMIC_RELEASE);
    Queue__wait_event();
    TEST_ASSERT_EQUAL(0, waits);
}

void test_readable_and_writable_waits_do_not_park_when_ready(void)
{
    __atomic_store_n(&incoming.produced, 1, __ATOMIC_RELEASE);
    Queue__wait_readable(&queue);
    Queue__wait_writable(&queue);
    TEST_ASSERT_EQUAL(0, waits);
}

void test_empty_read_and_full_write_park_until_peer_progress(void)
{
    pthread_t peer;
    TEST_ASSERT_EQUAL(0, pthread_create(&peer, NULL, publish_after_delay, NULL));
    Queue__wait_readable(&queue);
    TEST_ASSERT_EQUAL_UINT64(1, __atomic_load_n(&incoming.produced, __ATOMIC_ACQUIRE));
    TEST_ASSERT_EQUAL(0, pthread_join(peer, NULL));
    __atomic_store_n(&incoming.consumed, 0, __ATOMIC_RELEASE);
    queue.written = QUEUE_CAPACITY;
    TEST_ASSERT_EQUAL(0, pthread_create(&peer, NULL, publish_after_delay, NULL));
    Queue__wait_writable(&queue);
    TEST_ASSERT_EQUAL_UINT64(1, __atomic_load_n(&incoming.consumed, __ATOMIC_ACQUIRE));
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

void test_registration_change_and_empty_registry_return_without_parking(void)
{
    Queue__prepare_wait();
    QueueWait__forget(123);
    Queue__wait_event();
    TEST_ASSERT_EQUAL(0, waits);
    Queue__wait_event();
    TEST_ASSERT_EQUAL(0, waits);
}

void test_forgetting_unaccepted_arrival_is_idempotent(void)
{
    match_result(QueueWait__forget(456),
        success(code)
        {
            TEST_ASSERT_EQUAL(QUEUE_OK, code);
        },
        error_test_fail()
    );
    Queue__prepare_wait();
    pthread_t peer;
    TEST_ASSERT_EQUAL(0, pthread_create(&peer, NULL, publish_after_delay, NULL));
    Queue__wait_event();
    TEST_ASSERT_EQUAL(0, pthread_join(peer, NULL));
    TEST_ASSERT_GREATER_THAN(0, waits);
}

void test_arrival_wait_observes_callers_epoch(void)
{
    arrivals.endpoint = 17;
    arrivals.epoch = 2;
    QueueArrivals__wait(&arrivals, 1);
    TEST_ASSERT_EQUAL(0, waits);
    IpcPlatform__signal_event();
    pthread_t peer;
    TEST_ASSERT_EQUAL(0, pthread_create(&peer, NULL, publish_arrival_after_delay, NULL));
    QueueArrivals__wait(&arrivals, 2);
    TEST_ASSERT_EQUAL(0, pthread_join(peer, NULL));
    TEST_ASSERT_GREATER_THAN(0, waits);
}
