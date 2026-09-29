#include "unity.h"

#define main queue_wait_probe_main
#include "../../../../user/programs/queue_wait_probe/main.c"
#undef main

static uint64_t calls;
static uint64_t expected_ticks;
static SystemCallResult completion;

void setUp(void)
{
    calls = 0;
    expected_ticks = QUEUE_WAIT_DEFAULT_TICKS;
    completion = QUEUE_WAIT_TIMEOUT;
}

void tearDown(void)
{
}

SystemCallResult syscall(SystemCall number, Anything operation, Anything address, Anything count, Anything ticks,
                         Anything unused)
{
    TEST_ASSERT_EQUAL(SERVICE_CALL, number);
    TEST_ASSERT_EQUAL(SERVICE_QUEUE_WAIT, operation);
    TEST_ASSERT_EQUAL_UINT64(0, address);
    TEST_ASSERT_EQUAL_UINT64(0, count);
    TEST_ASSERT_EQUAL_UINT64(expected_ticks, ticks);
    calls++;
    return completion;
}

uint64_t process_current_pid(void)
{
    return 42;
}

int IO__printf(ConstBStringPtr format, ...)
{
    return 0;
}

Result(ProcessRecord)
    ProcessMetrics__snapshot_after(ConstProcessRegistryPtr registry, uint64_t pid, uint64_t sampled_after)
{
    TEST_ASSERT_EQUAL_UINT64(42, pid);
    return Success(ProcessRecord, ((ProcessRecord){.pid = pid, .sampled_at_usec = sampled_after}));
}

void test_summary_separates_timeout_durations_from_activity_and_errors(void)
{
    WaitProbeSummary summary = WaitProbeSummary__new();
    WaitProbeSummary__record(&summary, QUEUE_WAIT_TIMEOUT, 100);
    WaitProbeSummary__record(&summary, QUEUE_WAIT_TIMEOUT, 300);
    WaitProbeSummary__record(&summary, QUEUE_WAIT_CHANGED, 800);
    WaitProbeSummary__record(&summary, QUEUE_INVALID, 900);
    TEST_ASSERT_EQUAL_UINT64(2, summary.timeouts);
    TEST_ASSERT_EQUAL_UINT64(1, summary.activity);
    TEST_ASSERT_EQUAL_UINT64(1, summary.failures);
    TEST_ASSERT_EQUAL_UINT64(400, summary.timeout_usec);
    TEST_ASSERT_EQUAL_UINT64(2100, summary.call_usec);
    TEST_ASSERT_EQUAL_UINT64(100, summary.min_timeout_usec);
    TEST_ASSERT_EQUAL_UINT64(300, summary.max_timeout_usec);
}

void test_probe_runs_bounded_default_timeout_samples(void)
{
    char * argv[] = {"queue_wait_probe"};
    TEST_ASSERT_EQUAL(0, queue_wait_probe_main(1, argv));
    TEST_ASSERT_EQUAL_UINT64(WAIT_PROBE_SAMPLES, calls);
}

void test_probe_accepts_one_tick(void)
{
    char * argv[] = {"queue_wait_probe", "1"};
    expected_ticks = 1;
    TEST_ASSERT_EQUAL(0, queue_wait_probe_main(2, argv));
    TEST_ASSERT_EQUAL_UINT64(WAIT_PROBE_SAMPLES, calls);
}

void test_probe_rejects_invalid_bounds_before_syscall(void)
{
    char * zero[] = {"queue_wait_probe", "0"};
    char * large[] = {"queue_wait_probe", "101"};
    char * invalid[] = {"queue_wait_probe", "bad"};
    TEST_ASSERT_EQUAL(1, queue_wait_probe_main(2, zero));
    TEST_ASSERT_EQUAL(1, queue_wait_probe_main(2, large));
    TEST_ASSERT_EQUAL(1, queue_wait_probe_main(2, invalid));
    TEST_ASSERT_EQUAL_UINT64(0, calls);
}

void test_probe_does_not_report_success_when_no_wait_timed_out(void)
{
    char * argv[] = {"queue_wait_probe"};
    completion = QUEUE_WAIT_CHANGED;
    TEST_ASSERT_EQUAL(1, queue_wait_probe_main(1, argv));
    completion = QUEUE_INVALID;
    TEST_ASSERT_EQUAL(1, queue_wait_probe_main(1, argv));
}
