#include "unity.h"
#include "../../../kernel/scheduler/cpu_usage.h"

extern uint64_t test_cpu_time_usec;
static CPUUsage usage;

void setUp(void)
{
    test_cpu_time_usec = 0;
    usage = CPUUsage__new();
}

void tearDown(void)
{
}

void test_cpu_usage_new_process_has_no_usage(void)
{
    ProcessCPUUsage snapshot = CPUUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.total_usec);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.short_basis_points);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.long_basis_points);
}

void test_cpu_usage_counts_running_time_and_ages_sleeping_process(void)
{
    CPUUsage__start(&usage);
    test_cpu_time_usec = 50000;
    CPUUsage__stop(&usage);
    ProcessCPUUsage snapshot = CPUUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(50000, snapshot.total_usec);
    TEST_ASSERT_EQUAL_UINT64(5000, snapshot.short_basis_points);
    TEST_ASSERT_EQUAL_UINT64(500, snapshot.long_basis_points);
    test_cpu_time_usec = 125000;
    snapshot = CPUUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(2500, snapshot.short_basis_points);
    test_cpu_time_usec = 1050000;
    snapshot = CPUUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(50000, snapshot.total_usec);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.short_basis_points);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.long_basis_points);
}

void test_cpu_usage_accounts_active_slice_without_double_counting(void)
{
    CPUUsage__start(&usage);
    test_cpu_time_usec = 100000;
    ProcessCPUUsage snapshot = CPUUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(100000, snapshot.total_usec);
    TEST_ASSERT_EQUAL_UINT64(10000, snapshot.short_basis_points);
    test_cpu_time_usec = 150000;
    CPUUsage__stop(&usage);
    CPUUsage__stop(&usage);
    snapshot = CPUUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(150000, snapshot.total_usec);
    TEST_ASSERT_EQUAL_UINT64(10000, snapshot.short_basis_points);
    TEST_ASSERT_EQUAL_UINT64(1500, snapshot.long_basis_points);
}

void test_cpu_usage_handles_long_run_and_ring_wrap(void)
{
    CPUUsage__start(&usage);
    test_cpu_time_usec = 1000000000;
    ProcessCPUUsage snapshot = CPUUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(1000000000, snapshot.total_usec);
    TEST_ASSERT_EQUAL_UINT64(10000, snapshot.short_basis_points);
    TEST_ASSERT_EQUAL_UINT64(10000, snapshot.long_basis_points);
    CPUUsage__stop(&usage);
    test_cpu_time_usec += 3000000;
    CPUUsage__start(&usage);
    test_cpu_time_usec += 10000;
    CPUUsage__stop(&usage);
    snapshot = CPUUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(1000010000, snapshot.total_usec);
    TEST_ASSERT_EQUAL_UINT64(1000, snapshot.short_basis_points);
    TEST_ASSERT_EQUAL_UINT64(100, snapshot.long_basis_points);
}

void test_cpu_usage_counts_multiple_short_slices_in_same_tick(void)
{
    test_cpu_time_usec = 100;
    CPUUsage__start(&usage);
    test_cpu_time_usec = 300;
    CPUUsage__stop(&usage);
    test_cpu_time_usec = 700;
    CPUUsage__start(&usage);
    test_cpu_time_usec = 1200;
    CPUUsage__stop(&usage);
    ProcessCPUUsage snapshot = CPUUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(700, snapshot.total_usec);
    TEST_ASSERT_EQUAL_UINT64(50, snapshot.short_basis_points);
    test_cpu_time_usec = 2000;
    snapshot = CPUUsage__snapshot(&usage);
    TEST_ASSERT_EQUAL_UINT64(70, snapshot.short_basis_points);
}
