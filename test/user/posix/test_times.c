#include "unity.h"

#include <errno.h>
#include <sys/times.h>

#include "../../../common_includes/syscalls/process.h"

static ProcessTimesResult configured_status;
static ProcessTimes configured_times;

ProcessTimesResult process_times(ProcessTimesPtr times)
{
    if (configured_status == PROCESS_TIMES_OK)
    {
        *times = configured_times;
    }
    return configured_status;
}

#include "../../../user/lib/newlib/times.c"

void setUp(void)
{
    errno = 0;
    configured_status = PROCESS_TIMES_OK;
    configured_times = (ProcessTimes){
        .elapsed_usec = 100,
        .user_usec = 20,
        .system_usec = 10,
        .children_user_usec = 7,
        .children_system_usec = 3,
    };
}

void tearDown(void)
{
}

void test_times_returns_microsecond_ticks_and_fills_the_tms_snapshot(void)
{
    struct tms value = {0};

    TEST_ASSERT_EQUAL_INT64(100, times(&value));
    TEST_ASSERT_EQUAL_INT64(20, value.tms_utime);
    TEST_ASSERT_EQUAL_INT64(10, value.tms_stime);
    TEST_ASSERT_EQUAL_INT64(7, value.tms_cutime);
    TEST_ASSERT_EQUAL_INT64(3, value.tms_cstime);
}

void test_times_rejects_a_missing_snapshot(void)
{
    TEST_ASSERT_EQUAL_INT64(-1, times(0));
    TEST_ASSERT_EQUAL_INT(EFAULT, errno);
}

void test_times_maps_native_failure_to_einval(void)
{
    struct tms value = {0};
    configured_status = PROCESS_TIMES_INVALID_ARGUMENT;

    TEST_ASSERT_EQUAL_INT64(-1, times(&value));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}
