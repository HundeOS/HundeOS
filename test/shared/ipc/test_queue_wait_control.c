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
    IODiagnosticsSnapshot totals = IODiagnostics__snapshot(&control.diagnostics);
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

void test_block_diagnostics_accumulate_latency_and_failures(void)
{
    IODiagnostics__attach(&control.diagnostics);
    Optional(BlockDeviceTiming) first = BlockDeviceTiming__new(100, 110, 130, 160, 200, 250);
    Optional(BlockDeviceTiming) second = BlockDeviceTiming__new(300, 315, 340, 375, 420, 480);
    Optional(BlockDeviceTiming) no_timing = None(BlockDeviceTiming);
    IODiagnostics__record_block_submission(8, 100, true, &first);
    IODiagnostics__record_block_submission(1, 40, false, &second);
    IODiagnostics__record_block_submission(2, 0, true, &no_timing);
    IODiagnosticsSnapshot totals = IODiagnostics__snapshot(&control.diagnostics);

    TEST_ASSERT_EQUAL_UINT64(3, totals.block_submissions);
    TEST_ASSERT_EQUAL_UINT64(11, totals.block_blocks);
    TEST_ASSERT_EQUAL_UINT64(1, totals.block_failures);
    TEST_ASSERT_EQUAL_UINT64(2, totals.block_segmented_submissions);
    TEST_ASSERT_EQUAL_UINT64(47, totals.block_device.p50_usec);
    TEST_ASSERT_EQUAL_UINT64(111, totals.block_device.p70_usec);
    TEST_ASSERT_EQUAL_UINT64(111, totals.block_device.p90_usec);
    TEST_ASSERT_EQUAL_UINT64(111, totals.block_device.p99_usec);
    TEST_ASSERT_EQUAL_UINT64(11, totals.block_device_irq.p50_usec);
    TEST_ASSERT_EQUAL_UINT64(15, totals.block_device_irq.p70_usec);
    TEST_ASSERT_EQUAL_UINT64(23, totals.block_manager_wake.p50_usec);
    TEST_ASSERT_EQUAL_UINT64(27, totals.block_manager_wake.p70_usec);
    TEST_ASSERT_EQUAL_UINT64(31, totals.block_irq_relay.p50_usec);
    TEST_ASSERT_EQUAL_UINT64(39, totals.block_irq_relay.p70_usec);
    TEST_ASSERT_EQUAL_UINT64(47, totals.block_fiber_wake.p50_usec);
    TEST_ASSERT_EQUAL_UINT64(47, totals.block_fiber_wake.p70_usec);
    TEST_ASSERT_EQUAL_UINT64(55, totals.block_completion.p50_usec);
    TEST_ASSERT_EQUAL_UINT64(63, totals.block_completion.p70_usec);
    TEST_ASSERT_EQUAL_UINT64(0, IODiagnosticLatency__percentile(&control.diagnostics.block_completion, 0));
    TEST_ASSERT_EQUAL_UINT64(0, IODiagnosticLatency__percentile(&control.diagnostics.block_completion, 101));
}

void test_block_timing_rejects_out_of_order_timestamps(void)
{
    TEST_ASSERT_TRUE(is_nothing(BlockDeviceTiming__new(100, 99, 130, 160, 200, 250)));
    TEST_ASSERT_TRUE(is_nothing(BlockDeviceTiming__new(100, 110, 109, 160, 200, 250)));
    TEST_ASSERT_TRUE(is_nothing(BlockDeviceTiming__new(100, 110, 130, 129, 200, 250)));
    TEST_ASSERT_TRUE(is_nothing(BlockDeviceTiming__new(100, 110, 130, 160, 159, 250)));
    TEST_ASSERT_TRUE(is_nothing(BlockDeviceTiming__new(100, 110, 130, 160, 200, 199)));
}

void test_latency_histogram_records_general_samples(void)
{
    IODiagnosticLatency latency = {0};
    IODiagnosticLatency__record(&latency, 10);
    IODiagnosticLatency__record(&latency, 20);
    IODiagnosticLatency__record(&latency, 30);
    IODiagnosticLatency__record(&latency, 40);

    TEST_ASSERT_EQUAL_UINT64(23, IODiagnosticLatency__percentile(&latency, 50));
    TEST_ASSERT_EQUAL_UINT64(31, IODiagnosticLatency__percentile(&latency, 70));
    TEST_ASSERT_EQUAL_UINT64(47, IODiagnosticLatency__percentile(&latency, 90));
    TEST_ASSERT_EQUAL_UINT64(47, IODiagnosticLatency__percentile(&latency, 99));
}

void test_wait_publication_is_absent_until_committed_and_invalidated_before_update(void)
{
    match_optional(QueueWaitControl__publication(&control),
        some_test_fail(),
        none_do_nothing()
    );
    QueueWaitControl__begin(&control);
    match_optional(QueueWaitControl__publication(&control),
        some_test_fail(),
        none_do_nothing()
    );
    QueueWaitControl__commit(&control, 4321, 1234, 17);
    match_optional(
        QueueWaitControl__publication(&control),
        some(publication)
        {
            TEST_ASSERT_EQUAL_UINT64(4321, publication.started_at_usec);
            TEST_ASSERT_EQUAL_UINT64(1234, publication.deadline);
            TEST_ASSERT_EQUAL_UINT64(17, publication.observed_doorbell_generation);
            TEST_ASSERT_TRUE(QueueWaitControl__unchanged(&control, &publication));
            QueueWaitControl__clear(&control);
            TEST_ASSERT_FALSE(QueueWaitControl__unchanged(&control, &publication));
            QueueWaitControl__commit(&control, 8765, 5678, 18);
            TEST_ASSERT_FALSE(QueueWaitControl__unchanged(&control, &publication));
        },
        none_test_fail()
    );
}

void test_zero_doorbell_generation_is_a_valid_publication(void)
{
    QueueWaitControl__begin(&control);
    QueueWaitControl__commit(&control, 4321, 1234, 0);
    match_optional(QueueWaitControl__publication(&control),
        some(publication)
        {
            TEST_ASSERT_EQUAL_UINT64(0, publication.observed_doorbell_generation);
        },
        none_test_fail()
    );
}

void test_completed_wait_time_accumulates_without_leaving_a_live_publication(void)
{
    QueueWaitControl__begin(&control);
    QueueWaitControl__commit(&control, 1000, QUEUE_WAIT_NO_DEADLINE, 9);

    QueueWaitControl__complete_wait(&control, 4500);

    TEST_ASSERT_EQUAL_UINT64(3500, QueueWaitControl__completed_wait_usec(&control));
    match_optional(QueueWaitControl__publication(&control),
        some_test_fail(),
        none_do_nothing()
    );

    QueueWaitControl__begin(&control);
    QueueWaitControl__commit(&control, 5000, QUEUE_WAIT_NO_DEADLINE, 10);
    match_optional(
        QueueWaitControl__publication(&control),
        some(publication)
        {
            TEST_ASSERT_EQUAL_UINT64(3500, publication.completed_wait_usec);
        },
        none_test_fail()
    );
}

void test_publication_retains_the_observed_doorbell_generation(void)
{
    QueueWaitControl__begin(&control);
    QueueWaitControl__commit(&control, 4321, QUEUE_WAIT_NO_DEADLINE, 23);

    match_optional(
        QueueWaitControl__publication(&control),
        some(publication)
        {
            TEST_ASSERT_EQUAL_UINT64(23, publication.observed_doorbell_generation);
            TEST_ASSERT_TRUE(QueueWaitControl__unchanged(&control, &publication));
        },
        none_test_fail()
    );
}

void test_dispatch_trace_is_ticketed_stable_and_consumed_once(void)
{
    RequestTicket ticket = RequestTicket__new(17, 8, 42, &None(RequestTicket));
    QueueDispatchTrace trace = QueueDispatchTrace__new(&ticket, 110, 120, 125);
    QueueWaitControl__publish_dispatch(&control, &trace);

    RequestTicket other = RequestTicket__new(18, 1, 42, &None(RequestTicket));
    TEST_ASSERT_TRUE(is_nothing(QueueWaitControl__take_dispatch(&control, &other)));
    match_optional(
        QueueWaitControl__take_dispatch(&control, &ticket),
        some(observed)
        {
            TEST_ASSERT_TRUE(RequestTicket__equals(&ticket, &observed.ticket));
            TEST_ASSERT_EQUAL_UINT64(110, observed.eligible_at_usec);
            TEST_ASSERT_EQUAL_UINT64(120, observed.selected_at_usec);
            TEST_ASSERT_EQUAL_UINT64(125, observed.dispatched_at_usec);
        },
        none_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(QueueWaitControl__take_dispatch(&control, &ticket)));
}
