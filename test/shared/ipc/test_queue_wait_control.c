#include "unity.h"
#include "ipc/queue_wait_control.h"

static QueueWaitControl control;

void setUp(void)
{
    control = (QueueWaitControl){0};
}

void tearDown(void)
{
}

void test_diagnostics_accumulate_without_changing_wait_publication(void)
{
    IODiagnostics__attach(&control.diagnostics);
    IODiagnostics__record(IO_DIAGNOSTIC_UART_BYTES, 3);
    IODiagnostics__record(IO_DIAGNOSTIC_UART_BYTES, 2);
    IODiagnostics__record(IO_DIAGNOSTIC_BACKEND_EMPTY, 1);
    IODiagnostics__record(IO_DIAGNOSTIC_WARDEN_RELAYS, 4);
    IODiagnostics totals = IODiagnostics__snapshot(&control.diagnostics);
    TEST_ASSERT_EQUAL_UINT64(5, totals.uart_bytes);
    TEST_ASSERT_EQUAL_UINT64(1, totals.backend_empty);
    TEST_ASSERT_EQUAL_UINT64(4, totals.warden_relays);
    TEST_ASSERT_EQUAL_UINT64(0, totals.wait_entries);
    QueueWaitControl__clear(&control);
    totals = IODiagnostics__snapshot(&control.diagnostics);
    TEST_ASSERT_EQUAL_UINT64(5, totals.uart_bytes);
    match_optional(QueueWaitControl__publication(&control),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_wait_publication_is_absent_until_committed_and_invalidated_before_update(void)
{
    match_optional(QueueWaitControl__publication(&control),
        some_test_fail(),
        none_do_nothing()
    );
    QueueWaitSnapshot observed = {.kind = QUEUE_WAIT_CONNECTION, .handle = 12, .produced = 3};
    QueueWaitControl__begin(&control);
    match_result(QueueWaitControl__add(&control, &observed),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(QueueWaitControl__publication(&control),
        some_test_fail(),
        none_do_nothing()
    );
    QueueWaitControl__commit(&control, 4321, 1234, QueueWaitControl__wake_generation(&control));
    match_optional(
        QueueWaitControl__publication(&control),
        some(publication)
        {
            TEST_ASSERT_EQUAL_UINT64(1, publication.count);
            TEST_ASSERT_EQUAL_UINT64(4321, publication.started_at_usec);
            TEST_ASSERT_EQUAL_UINT64(1234, publication.deadline);
            TEST_ASSERT_EQUAL_UINT64(0, publication.observed_wake_generation);
            TEST_ASSERT_TRUE(QueueWaitControl__unchanged(&control, &publication));
            QueueWaitControl__clear(&control);
            TEST_ASSERT_FALSE(QueueWaitControl__unchanged(&control, &publication));
            QueueWaitControl__commit(&control, 8765, 5678, QueueWaitControl__wake_generation(&control));
            TEST_ASSERT_FALSE(QueueWaitControl__unchanged(&control, &publication));
        },
        none_test_fail()
    );
}

void test_empty_wait_never_suppresses_scheduling(void)
{
    QueueWaitControl__begin(&control);
    QueueWaitControl__commit(&control, 4321, 1234, QueueWaitControl__wake_generation(&control));
    match_optional(QueueWaitControl__publication(&control),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_completed_wait_time_accumulates_without_leaving_a_live_publication(void)
{
    QueueWaitSnapshot observed = {.kind = QUEUE_WAIT_CONNECTION, .handle = 12};
    QueueWaitControl__begin(&control);
    unwrap_result(QueueWaitControl__add(&control, &observed), ATOMIC_RECORD_INVALID);
    QueueWaitControl__commit(&control, 1000, QUEUE_WAIT_NO_DEADLINE, QueueWaitControl__wake_generation(&control));

    QueueWaitControl__complete_wait(&control, 4500);

    TEST_ASSERT_EQUAL_UINT64(3500, QueueWaitControl__completed_wait_usec(&control));
    match_optional(QueueWaitControl__publication(&control),
        some_test_fail(),
        none_do_nothing()
    );

    QueueWaitControl__begin(&control);
    unwrap_result(QueueWaitControl__add(&control, &observed), ATOMIC_RECORD_INVALID);
    QueueWaitControl__commit(&control, 5000, QUEUE_WAIT_NO_DEADLINE, QueueWaitControl__wake_generation(&control));
    match_optional(
        QueueWaitControl__publication(&control),
        some(publication)
        {
            TEST_ASSERT_EQUAL_UINT64(3500, publication.completed_wait_usec);
        },
        none_test_fail()
    );
}

void test_kernel_wake_generation_does_not_invalidate_publication(void)
{
    QueueWaitSnapshot observed = {.kind = QUEUE_WAIT_CONNECTION, .handle = 12};
    QueueWaitControl__begin(&control);
    unwrap_result(QueueWaitControl__add(&control, &observed), ATOMIC_RECORD_INVALID);
    QueueWaitWakeGeneration before = QueueWaitControl__wake_generation(&control);
    QueueWaitControl__commit(&control, 4321, QUEUE_WAIT_NO_DEADLINE, before);

    QueueWaitControl__wake(&control);

    TEST_ASSERT_TRUE(QueueWaitControl__woken(&control, before));
    match_optional(
        QueueWaitControl__publication(&control),
        some(publication)
        {
            TEST_ASSERT_EQUAL_UINT64(before, publication.observed_wake_generation);
            TEST_ASSERT_TRUE(QueueWaitControl__unchanged(&control, &publication));
        },
        none_test_fail()
    );
}
