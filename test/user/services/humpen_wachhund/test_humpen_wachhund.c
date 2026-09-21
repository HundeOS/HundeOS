#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/process.h"
#include "../../../../common_includes/syscalls/spawn.h"

static SpawnResult spawn_results[8];
static uint64_t spawn_result_count;
static uint64_t spawn_count;
static SpawnMode spawned_modes[8];
static char spawned_paths[8][96];
static char spawned_arg0[8][96];
static char spawned_arg1[8][96];
static char spawned_arg2[8][96];
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
    spawn_result_count = 0;
    spawn_count = 0;
    next_sleep_result = PROCESS_SLEEP_OK;
    sleep_count = 0;
    syslog_count = 0;
    syslog_tag[0] = '\0';
    syslog_message[0] = '\0';
    for (uint64_t i = 0; i < 8; i++)
    {
        spawn_results[i] = 0;
        spawned_modes[i] = 0;
        spawned_paths[i][0] = '\0';
        spawned_arg0[i][0] = '\0';
        spawned_arg1[i][0] = '\0';
        spawned_arg2[i][0] = '\0';
        sleep_durations[i] = 0;
    }
}

void tearDown(void)
{
}

Result(SpawnResult) OS__spawn(SpawnMode mode, ConstBStringPtr path, OptionalPtr(BString) arg0,
                              OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    TEST_ASSERT_LESS_THAN_UINT64(8, spawn_count);
    spawned_modes[spawn_count] = mode;
    snprintf(spawned_paths[spawn_count], sizeof(spawned_paths[spawn_count]), "%s", BString__cstr(path));
    match_optional(
        *arg0,
        some(value)
        {
            snprintf(spawned_arg0[spawn_count], sizeof(spawned_arg0[spawn_count]), "%s", BString__cstr(&value));
        },
        none_do_nothing()
    );
    match_optional(
        *arg1,
        some(value)
        {
            snprintf(spawned_arg1[spawn_count], sizeof(spawned_arg1[spawn_count]), "%s", BString__cstr(&value));
        },
        none_do_nothing()
    );
    match_optional(
        *arg2,
        some(value)
        {
            snprintf(spawned_arg2[spawn_count], sizeof(spawned_arg2[spawn_count]), "%s", BString__cstr(&value));
        },
        none_do_nothing()
    );

    SpawnResult result = 0;
    if (spawn_count < spawn_result_count)
    {
        result = spawn_results[spawn_count];
    }
    spawn_count++;

    if (result >= 0)
    {
        return Success(SpawnResult, result);
    }

    return Error(SpawnResult, bstr("spawn failed"), result);
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
    BString format_string = BString__new(format);
    BString message = BString__vsprintf(&format_string, args);
    va_end(args);

    int written = snprintf(syslog_message, sizeof(syslog_message), "%s", BString__cstr(&message));
    snprintf(syslog_tag, sizeof(syslog_tag), "%s", tag);
    syslog_count++;
    return written >= 0 ? SYSLOG_OK : SYSLOG_INVALID_ARGUMENT;
}

void test_humpen_wachhund_exits_when_mount_succeeds_immediately(void)
{
    char * argv[] = {"humpen-wachhund", "/dev/mmc0", "fat32", "/mnt/sd"};

    TEST_ASSERT_EQUAL_INT(0, humpen_wachhund_main_under_test(4, argv));
    TEST_ASSERT_EQUAL_UINT64(1, spawn_count);
    TEST_ASSERT_EQUAL(P_WAIT, spawned_modes[0]);
    TEST_ASSERT_EQUAL_STRING(HUMPEN_WACHHUND_FAT32FS_PATH, spawned_paths[0]);
    TEST_ASSERT_EQUAL_STRING("/dev/mmc0", spawned_arg0[0]);
    TEST_ASSERT_EQUAL_STRING("/mnt/sd", spawned_arg1[0]);
    TEST_ASSERT_EQUAL_STRING("", spawned_arg2[0]);
    TEST_ASSERT_EQUAL_UINT64(0, sleep_count);
    TEST_ASSERT_EQUAL_UINT64(1, syslog_count);
    TEST_ASSERT_EQUAL_STRING("humpen-wachhund", syslog_tag);
    TEST_ASSERT_EQUAL_STRING("mounted /dev/mmc0 on /mnt/sd", syslog_message);
}

void test_humpen_wachhund_rejects_unsupported_filesystem_without_retry(void)
{
    char * argv[] = {"humpen-wachhund", "/dev/mmc0", "bootstrap-ext2", "/mnt/sd"};
    TEST_ASSERT_EQUAL_INT(1, humpen_wachhund_main_under_test(4, argv));
    TEST_ASSERT_EQUAL_UINT64(0, spawn_count);
    TEST_ASSERT_EQUAL_UINT64(0, sleep_count);
}

void test_humpen_wachhund_retries_with_exponential_backoff(void)
{
    char * argv[] = {"humpen-wachhund", "/dev/mmc0", "fat32", "/mnt/sd"};
    spawn_results[0] = 1;
    spawn_results[1] = 1;
    spawn_results[2] = 0;
    spawn_result_count = 3;

    TEST_ASSERT_EQUAL_INT(0, humpen_wachhund_main_under_test(4, argv));
    TEST_ASSERT_EQUAL_UINT64(3, spawn_count);
    TEST_ASSERT_EQUAL_UINT64(2, sleep_count);
    TEST_ASSERT_EQUAL_UINT64(HUMPEN_WACHHUND_INITIAL_BACKOFF_USEC, sleep_durations[0]);
    TEST_ASSERT_EQUAL_UINT64(HUMPEN_WACHHUND_INITIAL_BACKOFF_USEC * 2ULL, sleep_durations[1]);
    TEST_ASSERT_EQUAL_STRING("mounted /dev/mmc0 on /mnt/sd", syslog_message);
}

void test_humpen_wachhund_exits_when_sleep_fails(void)
{
    char * argv[] = {"humpen-wachhund", "/dev/mmc0", "fat32", "/mnt/sd"};
    spawn_results[0] = 1;
    spawn_result_count = 1;
    next_sleep_result = PROCESS_SLEEP_FAILED;

    TEST_ASSERT_EQUAL_INT(1, humpen_wachhund_main_under_test(4, argv));
    TEST_ASSERT_EQUAL_UINT64(1, sleep_count);
    char expected_message[32];
    snprintf(expected_message, sizeof(expected_message), "sleep failed: %i", PROCESS_SLEEP_FAILED);
    TEST_ASSERT_EQUAL_STRING(expected_message, syslog_message);
}

void test_humpen_wachhund_runs_ext2fs_driver_in_wait_mode(void)
{
    char * argv[] = {"humpen-wachhund", "/dev/mmc0", "ext2", "/mnt/sd"};

    TEST_ASSERT_EQUAL_INT(0, humpen_wachhund_main_under_test(4, argv));

    TEST_ASSERT_EQUAL_UINT64(1, spawn_count);
    TEST_ASSERT_EQUAL(P_WAIT, spawned_modes[0]);
    TEST_ASSERT_EQUAL_STRING(HUMPEN_WACHHUND_EXT2FS_PATH, spawned_paths[0]);
    TEST_ASSERT_EQUAL_STRING("/dev/mmc0", spawned_arg0[0]);
    TEST_ASSERT_EQUAL_STRING("/mnt/sd", spawned_arg1[0]);
    TEST_ASSERT_EQUAL_STRING("", spawned_arg2[0]);
    TEST_ASSERT_EQUAL_UINT64(0, sleep_count);
    TEST_ASSERT_EQUAL_STRING("mounted /dev/mmc0 on /mnt/sd", syslog_message);
}

void test_humpen_wachhund_retries_ext2fs_driver_when_it_exits_with_failure(void)
{
    char * argv[] = {"humpen-wachhund", "/dev/mmc0", "ext2", "/mnt/sd"};
    spawn_results[0] = 1;
    spawn_results[1] = 0;
    spawn_result_count = 2;

    TEST_ASSERT_EQUAL_INT(0, humpen_wachhund_main_under_test(4, argv));

    TEST_ASSERT_EQUAL_UINT64(2, spawn_count);
    TEST_ASSERT_EQUAL(P_WAIT, spawned_modes[0]);
    TEST_ASSERT_EQUAL(P_WAIT, spawned_modes[1]);
    TEST_ASSERT_EQUAL_UINT64(1, sleep_count);
    TEST_ASSERT_EQUAL_UINT64(HUMPEN_WACHHUND_INITIAL_BACKOFF_USEC, sleep_durations[0]);
    TEST_ASSERT_EQUAL_STRING("mounted /dev/mmc0 on /mnt/sd", syslog_message);
}

void test_humpen_wachhund_retries_ext2fs_driver_when_spawn_fails(void)
{
    char * argv[] = {"humpen-wachhund", "/dev/mmc0", "ext2", "/mnt/sd"};
    spawn_results[0] = SPAWN_NO_FILE;
    spawn_results[1] = 0;
    spawn_result_count = 2;

    TEST_ASSERT_EQUAL_INT(0, humpen_wachhund_main_under_test(4, argv));

    TEST_ASSERT_EQUAL_UINT64(2, spawn_count);
    TEST_ASSERT_EQUAL_UINT64(1, sleep_count);
    TEST_ASSERT_EQUAL_UINT64(HUMPEN_WACHHUND_INITIAL_BACKOFF_USEC, sleep_durations[0]);
    TEST_ASSERT_EQUAL_STRING("mounted /dev/mmc0 on /mnt/sd", syslog_message);
}
