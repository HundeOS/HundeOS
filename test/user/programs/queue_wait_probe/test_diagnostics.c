#include "unity.h"
#include <pthread.h>
#include <time.h>
#include "../../../../user/lib/sys/process_metrics.c"

static ProcessRegistry registry;
static ProcessInfo listed_processes[64];
static ProcessListResult listed_count;

Result(Anything) mem_alloc_bytes(size_t size)
{
    return Success(Anything, (Anything)malloc(size));
}

ProcessListResult process_list(ProcessInfoPtr entries, size_t max_entries)
{
    ProcessListResult copied =
        listed_count < (ProcessListResult)max_entries ? listed_count : (ProcessListResult)max_entries;
    for (ProcessListResult index = 0; index < copied; index++)
    {
        entries[index] = listed_processes[index];
    }
    return copied;
}

void setUp(void)
{
    registry.version = PROCESS_REGISTRY_VERSION;
    ProcessRecord record = {.pid = 42, .sampled_at_usec = 100, .queue_wait_usage = {.parks = 16}};
    ProcessRegistry__set(&registry, 0, 0, &record);
    ProcessRegistry__set_count(&registry, 0, 1);
    listed_count = 0;
}

void test_process_info_uses_exact_process_list_snapshot(void)
{
    listed_processes[0] = (ProcessInfo){.pid = 7, .cpu_usage = {.scheduled_usec = 100}};
    listed_processes[1] = (ProcessInfo){.pid = 42, .cpu_usage = {.scheduled_usec = 900}};
    listed_count = 2;

    match_result(
        ProcessMetrics__info(42),
        success(info)
        {
            TEST_ASSERT_EQUAL_UINT64(900, info.cpu_usage.scheduled_usec);
        },
        error_test_fail()
    );
}

void tearDown(void)
{
}

static void * publish_fresh_record(void * context)
{
    struct timespec delay = {.tv_nsec = 1000000};
    nanosleep(&delay, NULL);
    ProcessRecord record = {.pid = 42, .sampled_at_usec = 200, .queue_wait_usage = {.parks = 32}};
    ProcessRegistry__set(&registry, 0, 0, &record);
    return context;
}

void test_diagnostics_returns_fresh_counters(void)
{
    match_result(
        ProcessMetrics__snapshot_after(&registry, 42, 100),
        success(record)
        {
            TEST_ASSERT_EQUAL_UINT64(16, record.queue_wait_usage.parks);
        },
        error_test_fail()
    );
}

void test_diagnostics_waits_for_publication_after_measurement(void)
{
    pthread_t publisher;
    TEST_ASSERT_EQUAL(0, pthread_create(&publisher, NULL, publish_fresh_record, NULL));
    Result(ProcessRecord) result = ProcessMetrics__snapshot_after(&registry, 42, 200);
    TEST_ASSERT_EQUAL(0, pthread_join(publisher, NULL));
    match_result(
        result,
        success(record)
        {
            TEST_ASSERT_EQUAL_UINT64(32, record.queue_wait_usage.parks);
        },
        error_test_fail()
    );
}

void test_diagnostics_rejects_missing_process_and_bad_registry(void)
{
    match_result(
        ProcessMetrics__snapshot_after(&registry, 43, 100),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(0, code);
        }
    );
    registry.version = 0;
    match_result(
        ProcessMetrics__snapshot_after(&registry, 42, 100),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(0, code);
        }
    );
}
