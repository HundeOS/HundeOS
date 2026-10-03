#include "unity.h"

#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/kalloc.h"
#include "../stubs/page_table.h"
#include "../../../kernel/service/clock.h"

static KernelPtr kernel;
static ProcessPtr process;

void setUp(void)
{
    kernel = init_test_kernel();
    process = dummy_process(20, 0, PROC_STATE_READY);
}

void tearDown(void)
{
    KAllocStub__set_user_alloc_failure(false);
}

void test_clock_maps_read_only_and_non_executable(void)
{
    TEST_ASSERT_TRUE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), CLOCK_PAGE_VADDR, true,
                                          false, false, CLOCK_PAGE_SIZE));
    TEST_ASSERT_TRUE(kernel->clock.mapping.read);
    TEST_ASSERT_FALSE(kernel->clock.mapping.write);
    TEST_ASSERT_FALSE(kernel->clock.mapping.execute);
}

void test_clock_page_contains_the_boot_timer_frequency(void)
{
    ClockPagePtr page = k_v_ptr(kernel->clock.mapping, ClockPage);

    match_optional(
        ClockPage__snapshot(page),
        some(snapshot)
        {
            TEST_ASSERT_EQUAL_UINT64(0, snapshot.timer_frequency);
            match_optional(
                snapshot.realtime_clock,
                some(sample)
                {
                    TEST_ASSERT_EQUAL_UINT64(1700000000000000ULL, sample.unix_usec);
                    TEST_ASSERT_EQUAL_UINT64(123000, sample.monotonic_usec);
                },
                none_test_fail()
            );
        },
        none_test_fail()
    );
}

void test_clock_allocation_failure_does_not_publish_a_mapping(void)
{
    KernelClock clock = KernelClock__new();
    Optional(RealtimeClockSample) realtime_clock = None(RealtimeClockSample);
    KAllocStub__set_user_alloc_failure(true);

    match_result(
        KernelClock__init(&clock, 10000000, &realtime_clock),
        success(code)
        {
            TEST_FAIL();
        },
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_MAP_FAILED, code);
        }
    );
    TEST_ASSERT_FALSE(clock.mapping.valid);
}
