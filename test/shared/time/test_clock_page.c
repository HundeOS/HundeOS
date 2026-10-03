#include "unity.h"

#include "time/clock_page.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_clock_page_publishes_a_stable_snapshot(void)
{
    ClockPage page = {0};

    RealtimeClockSample realtime = RealtimeClockSample__new(1700000000000000ULL, 123000ULL);
    Optional(RealtimeClockSample) realtime_clock = Some(RealtimeClockSample, realtime);
    ClockPage__publish(&page, 10000000, &realtime_clock);

    match_optional(
        ClockPage__snapshot(&page),
        some(snapshot)
        {
            TEST_ASSERT_EQUAL_UINT64(10000000, snapshot.timer_frequency);
            TEST_ASSERT_EQUAL_UINT64(2, page.sequence);
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

void test_clock_page_republication_advances_the_sequence(void)
{
    ClockPage page = {0};
    Optional(RealtimeClockSample) realtime_clock = None(RealtimeClockSample);
    ClockPage__publish(&page, 100, &realtime_clock);

    ClockPage__publish(&page, 200, &realtime_clock);

    match_optional(
        ClockPage__snapshot(&page),
        some(snapshot)
        {
            TEST_ASSERT_EQUAL_UINT64(200, snapshot.timer_frequency);
            TEST_ASSERT_EQUAL_UINT64(4, page.sequence);
            match_optional(snapshot.realtime_clock,
                some_test_fail(),
                none_do_nothing()
            );
        },
        none_test_fail()
    );
}

void test_clock_page_rejects_an_in_progress_or_invalid_publication(void)
{
    ClockPage page = {0};
    Optional(RealtimeClockSample) realtime_clock = None(RealtimeClockSample);
    ClockPage__publish(&page, 100, &realtime_clock);
    __atomic_store_n(&page.sequence, 3, __ATOMIC_RELEASE);

    match_optional(ClockPage__snapshot(&page),
        some_test_fail(),
        none_do_nothing()
    );

    __atomic_store_n(&page.sequence, 4, __ATOMIC_RELEASE);
    __atomic_store_n(&page.magic, 0, __ATOMIC_RELAXED);
    match_optional(ClockPage__snapshot(&page),
        some_test_fail(),
        none_do_nothing()
    );
}
