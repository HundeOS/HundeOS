#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/process.h"

static IOStatus mount_results[8];
static uint64_t mount_result_count;
static uint64_t mount_count;
static ProcessSleepResult next_sleep_result;
static uint64_t sleep_count;
static uint64_t sleep_durations[8];
static uint64_t syslog_count;
static char syslog_tag[64];
static char syslog_message[512];

#define HUMPEN_WACHHUND_ENTRYPOINT humpen_wachhund_main_under_test
#include "../../../../user/services/humpen-wachhund/main.c"

void setUp(void)
{
    mount_result_count = 0;
    mount_count = 0;
    next_sleep_result = PROCESS_SLEEP_OK;
    sleep_count = 0;
    syslog_count = 0;
    syslog_tag[0] = '\0';
    syslog_message[0] = '\0';
    for (uint64_t i = 0; i < 8; i++)
    {
        mount_results[i] = IO_OK;
        sleep_durations[i] = 0;
    }
}

void tearDown(void)
{
}

IOStatus IO__mount_device(ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint)
{
    TEST_ASSERT_EQUAL_STRING("/dev/mmc0", BString__cstr(device_path));
    TEST_ASSERT_EQUAL_STRING("fat32", BString__cstr(filesystem_type));
    TEST_ASSERT_EQUAL_STRING("/mnt/sd", BString__cstr(mountpoint));

    IOStatus result = IO_OK;
    if (mount_count < mount_result_count)
    {
        result = mount_results[mount_count];
    }
    mount_count++;
    return result;
}

ProcessSleepResult sleep_usec(uint64_t usec)
{
    TEST_ASSERT_LESS_THAN_UINT64(8, sleep_count);
    sleep_durations[sleep_count] = usec;
    sleep_count++;
    return next_sleep_result;
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    va_list args;
    va_start(args, format);
    int written = vsnprintf(syslog_message, sizeof(syslog_message), format, args);
    va_end(args);

    snprintf(syslog_tag, sizeof(syslog_tag), "%s", tag);
    syslog_count++;
    return written >= 0 ? SYSLOG_OK : SYSLOG_INVALID_ARGUMENT;
}

void test_humpen_wachhund_exits_when_mount_succeeds_immediately(void)
{
    char * argv[] = {"humpen-wachhund", "/dev/mmc0", "fat32", "/mnt/sd"};

    TEST_ASSERT_EQUAL_INT(0, humpen_wachhund_main_under_test(4, argv));
    TEST_ASSERT_EQUAL_UINT64(1, mount_count);
    TEST_ASSERT_EQUAL_UINT64(0, sleep_count);
    TEST_ASSERT_EQUAL_UINT64(1, syslog_count);
    TEST_ASSERT_EQUAL_STRING("humpen-wachhund", syslog_tag);
    TEST_ASSERT_EQUAL_STRING("mounted /dev/mmc0 on /mnt/sd", syslog_message);
}

void test_humpen_wachhund_retries_with_exponential_backoff(void)
{
    char * argv[] = {"humpen-wachhund", "/dev/mmc0", "fat32", "/mnt/sd"};
    mount_results[0] = IO_INVALID_PATH;
    mount_results[1] = IO_INVALID_PATH;
    mount_results[2] = IO_OK;
    mount_result_count = 3;

    TEST_ASSERT_EQUAL_INT(0, humpen_wachhund_main_under_test(4, argv));
    TEST_ASSERT_EQUAL_UINT64(3, mount_count);
    TEST_ASSERT_EQUAL_UINT64(2, sleep_count);
    TEST_ASSERT_EQUAL_UINT64(HUMPEN_WACHHUND_INITIAL_BACKOFF_USEC, sleep_durations[0]);
    TEST_ASSERT_EQUAL_UINT64(HUMPEN_WACHHUND_INITIAL_BACKOFF_USEC * 2ULL, sleep_durations[1]);
    TEST_ASSERT_EQUAL_STRING("mounted /dev/mmc0 on /mnt/sd", syslog_message);
}

void test_humpen_wachhund_exits_when_sleep_fails(void)
{
    char * argv[] = {"humpen-wachhund", "/dev/mmc0", "fat32", "/mnt/sd"};
    mount_results[0] = IO_INVALID_PATH;
    mount_result_count = 1;
    next_sleep_result = PROCESS_SLEEP_FAILED;

    TEST_ASSERT_EQUAL_INT(1, humpen_wachhund_main_under_test(4, argv));
    TEST_ASSERT_EQUAL_UINT64(1, sleep_count);
    char expected_message[32];
    snprintf(expected_message, sizeof(expected_message), "sleep failed: %i", PROCESS_SLEEP_FAILED);
    TEST_ASSERT_EQUAL_STRING(expected_message, syslog_message);
}
