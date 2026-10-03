#include "unity.h"

#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include "../../../common/time/clock_page.h"

static uint64_t configured_frequency;
static uint64_t monotonic_usec;

static void configure_frequency(uint64_t frequency)
{
    configured_frequency = frequency;
}

static uint64_t read_monotonic_usec(void)
{
    return monotonic_usec;
}

#include "../../../user/lib/newlib/clock.c"

static ClockPage page;

void setUp(void)
{
    page = (ClockPage){0};
    Optional(RealtimeClockSample) realtime_clock =
        Some(RealtimeClockSample, RealtimeClockSample__new(1700000000123456ULL, 12000000ULL));
    ClockPage__publish(&page, 10000000, &realtime_clock);
    configured_frequency = 0;
    monotonic_usec = 12345678;
    errno = 0;
}

void test_gettimeofday_advances_the_boot_realtime_sample_with_monotonic_time(void)
{
    struct timeval value = {0};

    TEST_ASSERT_EQUAL_INT(0, Clock__gettimeofday_from(&page, &value, configure_frequency, read_monotonic_usec));
    TEST_ASSERT_EQUAL_UINT64(10000000, configured_frequency);
    TEST_ASSERT_EQUAL_INT64(1700000000, value.tv_sec);
    TEST_ASSERT_EQUAL_INT64(469134, value.tv_usec);
}

void test_gettimeofday_rejects_a_missing_realtime_sample(void)
{
    struct timeval value = {.tv_sec = 4, .tv_usec = 5};
    Optional(RealtimeClockSample) realtime_clock = None(RealtimeClockSample);
    ClockPage__publish(&page, 10000000, &realtime_clock);

    TEST_ASSERT_EQUAL_INT(-1, Clock__gettimeofday_from(&page, &value, configure_frequency, read_monotonic_usec));
    TEST_ASSERT_EQUAL_INT(ENOSYS, errno);
    TEST_ASSERT_EQUAL_INT64(0, value.tv_sec);
    TEST_ASSERT_EQUAL_INT64(0, value.tv_usec);
}

void test_gettimeofday_rejects_a_missing_result_pointer(void)
{
    TEST_ASSERT_EQUAL_INT(-1, Clock__gettimeofday_from(&page, NULL, configure_frequency, read_monotonic_usec));
    TEST_ASSERT_EQUAL_INT(EFAULT, errno);
}

void test_gettimeofday_rejects_monotonic_time_before_the_boot_sample(void)
{
    struct timeval value = {0};
    monotonic_usec = 11999999;

    TEST_ASSERT_EQUAL_INT(-1, Clock__gettimeofday_from(&page, &value, configure_frequency, read_monotonic_usec));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}

void tearDown(void)
{
}

void test_clock_gettime_reads_monotonic_time_from_the_shared_parameters(void)
{
    struct timespec value = {0};

    TEST_ASSERT_EQUAL_INT(
        0, Clock__gettime_from(&page, CLOCK_MONOTONIC, &value, configure_frequency, read_monotonic_usec));
    TEST_ASSERT_EQUAL_UINT64(10000000, configured_frequency);
    TEST_ASSERT_EQUAL_INT64(12, value.tv_sec);
    TEST_ASSERT_EQUAL_INT64(345678000, value.tv_nsec);
}

void test_clock_gettime_rejects_unsupported_clocks(void)
{
    struct timespec value = {0};

    TEST_ASSERT_EQUAL_INT(-1, Clock__gettime_from(&page, 99, &value, configure_frequency, read_monotonic_usec));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}

void test_clock_gettime_rejects_a_missing_result_pointer(void)
{
    TEST_ASSERT_EQUAL_INT(-1,
                          Clock__gettime_from(&page, CLOCK_MONOTONIC, NULL, configure_frequency, read_monotonic_usec));
    TEST_ASSERT_EQUAL_INT(EFAULT, errno);
}

void test_clock_gettime_rejects_an_invalid_clock_page(void)
{
    struct timespec value = {0};
    page.magic = 0;

    TEST_ASSERT_EQUAL_INT(
        -1, Clock__gettime_from(&page, CLOCK_MONOTONIC, &value, configure_frequency, read_monotonic_usec));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}
