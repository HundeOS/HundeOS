#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/process.h"

static ProcessInfo fake_processes[4];
static ProcessListResult fake_process_count;
static ProcessListResult fake_process_result;
static char output[512];
static uint64_t output_position;

#define PS_ENTRYPOINT ps_main_under_test
#include "../../../../user/commands/ps/main.c"

void setUp(void)
{
    fake_process_count = 0;
    fake_process_result = 0;
    output_position = 0;
    output[0] = '\0';
    for (uint64_t i = 0; i < 4; i++)
    {
        fake_processes[i] = (ProcessInfo) { 0 };
    }
}

void tearDown(void)
{
}

static void copy_text(char * target, const char * source, uint64_t target_size)
{
    uint64_t position = 0;
    while (position + 1 < target_size && source[position] != '\0')
    {
        target[position] = source[position];
        position++;
    }
    target[position] = '\0';
}

ProcessListResult process_list(ProcessInfoPtr entries, size_t max_entries)
{
    if (fake_process_result < 0)
    {
        return fake_process_result;
    }

    uint64_t count = fake_process_count;
    if (count > max_entries)
    {
        count = max_entries;
    }

    for (uint64_t i = 0; i < count; i++)
    {
        entries[i] = fake_processes[i];
    }
    return (ProcessListResult) count;
}

int printf(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    int written = vsnprintf(output + output_position, sizeof(output) - output_position, format, args);
    va_end(args);
    if (written > 0)
    {
        output_position += (uint64_t) written;
    }
    return written;
}

void test_ps_prints_process_snapshot(void)
{
    fake_process_count = 2;
    fake_processes[0].pid = 1;
    copy_text(fake_processes[0].state, "PROC_STATE_READY", PROCESS_INFO_STATE_SIZE);
    copy_text(fake_processes[0].wait_reason, "-", PROCESS_INFO_WAIT_SIZE);
    copy_text(fake_processes[0].class_name, "normal", PROCESS_INFO_CLASS_SIZE);
    copy_text(fake_processes[0].user_name, "root", PROCESS_INFO_USER_SIZE);
    copy_text(fake_processes[0].name, "init", PROCESS_INFO_NAME_SIZE);
    fake_processes[1].pid = 2;
    copy_text(fake_processes[1].state, "PROC_STATE_WAITING", PROCESS_INFO_STATE_SIZE);
    copy_text(fake_processes[1].wait_reason, "PARK_WAIT_SERVICE_EVENT", PROCESS_INFO_WAIT_SIZE);
    copy_text(fake_processes[1].class_name, "service", PROCESS_INFO_CLASS_SIZE);
    copy_text(fake_processes[1].user_name, "hund", PROCESS_INFO_USER_SIZE);
    copy_text(fake_processes[1].name, "virtio_blk", PROCESS_INFO_NAME_SIZE);

    TEST_ASSERT_EQUAL_INT(0, ps_main_under_test());
    TEST_ASSERT_EQUAL_STRING(
        "PID STATE WAIT CLASS USER NAME\n"
        "1 PROC_STATE_READY - normal root init\n"
        "2 PROC_STATE_WAITING PARK_WAIT_SERVICE_EVENT service hund virtio_blk\n",
        output
    );
}

void test_ps_reports_process_list_error(void)
{
    fake_process_result = PROCESS_LIST_INVALID_ARGUMENT;

    TEST_ASSERT_EQUAL_INT(1, ps_main_under_test());
    TEST_ASSERT_EQUAL_STRING("ps: error -1\n", output);
}
