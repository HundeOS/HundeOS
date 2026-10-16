#include "unity.h"

#include <errno.h>
#include <sys/times.h>
#include <unistd.h>

#include "../../../user/lib/newlib/sysconf.c"

void setUp(void)
{
    errno = 0;
}

void tearDown(void)
{
}

void test_sysconf_reports_the_times_tick_rate(void)
{
    TEST_ASSERT_EQUAL_INT64(CLK_TCK, sysconf(_SC_CLK_TCK));
    TEST_ASSERT_EQUAL_INT(0, errno);
}

void test_sysconf_rejects_an_unsupported_name(void)
{
    TEST_ASSERT_EQUAL_INT64(-1, sysconf(_SC_CLK_TCK + 1));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}
