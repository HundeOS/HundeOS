#include "unity.h"
#include "../../../kernel/scheduler/queue_wait_usage.h"

extern uint64_t test_cpu_time_usec;
static QueueWaitUsage usage;

void setUp(void)
{
    test_cpu_time_usec = 0;
    usage = QueueWaitUsage__new();
}

void tearDown(void)
{
}

void test_queue_wait_usage_counts_completed_intervals_and_wake_reasons(void)
{
    QueueWaitUsage__park(&usage);
    test_cpu_time_usec = 25000;
    QueueWaitUsage__wake(&usage, QUEUE_WAIT_TIMEOUT);
    test_cpu_time_usec = 30000;
    QueueWaitUsage__select(&usage);
    test_cpu_time_usec = 40000;
    QueueWaitUsage__park(&usage);
    test_cpu_time_usec = 41000;
    QueueWaitUsage__wake(&usage, QUEUE_WAIT_CHANGED);
    test_cpu_time_usec = 43000;
    QueueWaitUsage__select(&usage);
    ProcessQueueWaitUsage snapshot = QueueWaitUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(2, snapshot.parks);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.activity_wakes);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.timeout_wakes);
    TEST_ASSERT_EQUAL_UINT64(2, snapshot.selections);
    TEST_ASSERT_EQUAL_UINT64(26000, snapshot.parked_usec);
    TEST_ASSERT_EQUAL_UINT64(25000, snapshot.max_parked_usec);
    TEST_ASSERT_EQUAL_UINT64(7000, snapshot.runnable_usec);
    TEST_ASSERT_EQUAL_UINT64(5000, snapshot.max_runnable_usec);
}

void test_queue_wait_usage_does_not_count_unrelated_or_duplicate_transitions(void)
{
    QueueWaitUsage__wake(&usage, QUEUE_WAIT_TIMEOUT);
    QueueWaitUsage__select(&usage);
    TEST_ASSERT_EQUAL_UINT64(0, QueueWaitUsage__snapshot(&usage).selections);
    QueueWaitUsage__park(&usage);
    test_cpu_time_usec = 100;
    QueueWaitUsage__wake(&usage, QUEUE_WAIT_PARKED);
    QueueWaitUsage__select(&usage);
    TEST_ASSERT_EQUAL_UINT64(0, QueueWaitUsage__snapshot(&usage).parked_usec);
    QueueWaitUsage__wake(&usage, QUEUE_WAIT_CHANGED);
    test_cpu_time_usec = 200;
    QueueWaitUsage__wake(&usage, QUEUE_WAIT_TIMEOUT);
    QueueWaitUsage__select(&usage);
    test_cpu_time_usec = 300;
    QueueWaitUsage__select(&usage);
    ProcessQueueWaitUsage snapshot = QueueWaitUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.activity_wakes);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.timeout_wakes);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.selections);
    TEST_ASSERT_EQUAL_UINT64(100, snapshot.parked_usec);
    TEST_ASSERT_EQUAL_UINT64(100, snapshot.runnable_usec);
}

void test_queue_wait_usage_snapshot_excludes_incomplete_intervals(void)
{
    QueueWaitUsage__park(&usage);
    test_cpu_time_usec = 100;
    ProcessQueueWaitUsage snapshot = QueueWaitUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.parks);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.parked_usec);
    QueueWaitUsage__wake(&usage, QUEUE_WAIT_TIMEOUT);
    test_cpu_time_usec = 200;
    snapshot = QueueWaitUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.timeout_wakes);
    TEST_ASSERT_EQUAL_UINT64(100, snapshot.parked_usec);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.selections);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.runnable_usec);
}

void test_queue_wait_usage_counts_speculative_selections_independently(void)
{
    QueueWaitUsage__select_speculative(&usage);
    QueueWaitUsage__select_speculative(&usage);

    ProcessQueueWaitUsage snapshot = QueueWaitUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(2, snapshot.speculative_selections);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.selections);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.parks);
}
