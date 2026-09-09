#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/process.h"

static uint64_t last_pid;
static ProcessSignal last_signal;
static ProcessSignalResult fake_result;
static char output[256];
static uint64_t output_position;

#define main kill_main_under_test
#include "../../../../user/commands/kill/main.c"

void setUp(void)
{
    last_pid = 0;
    last_signal = 0;
    fake_result = PROCESS_SIGNAL_OK;
    output_position = 0;
    output[0] = '\0';
}

void tearDown(void)
{
}

ProcessSignalResult kill_process(uint64_t pid, ProcessSignal signal)
{
    last_pid = pid;
    last_signal = signal;
    return fake_result;
}

void IO__print(ConstBStringPtr message)
{
    const char * str = BString__cstr(message);
    while (*str != '\0' && output_position + 1 < sizeof(output))
    {
        output[output_position++] = *str++;
    }
    output[output_position] = '\0';
}

int IO__printf(ConstBStringPtr format, ...)
{
    va_list args;
    va_start(args, format);
    BString formatted = BString__vsprintf(format, args);
    va_end(args);
    StringPos written = BString__copy_to(&formatted, output + output_position, sizeof(output) - output_position);
    if (written > 0)
    {
        output_position += (uint64_t)written;
    }
    return (int)written;
}

void test_kill_defaults_to_terminate_signal(void)
{
    char * argv[] = {"kill", "42"};

    TEST_ASSERT_EQUAL_INT(0, kill_main_under_test(2, argv));
    TEST_ASSERT_EQUAL_UINT64(42, last_pid);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_SIGNAL_TERMINATE, last_signal);
}

void test_kill_accepts_named_signal(void)
{
    char * argv[] = {"kill", "-KILL", "77"};

    TEST_ASSERT_EQUAL_INT(0, kill_main_under_test(3, argv));
    TEST_ASSERT_EQUAL_UINT64(77, last_pid);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_SIGNAL_KILL, last_signal);
}

void test_kill_accepts_numeric_signal(void)
{
    char * argv[] = {"kill", "-2", "9"};

    TEST_ASSERT_EQUAL_INT(0, kill_main_under_test(3, argv));
    TEST_ASSERT_EQUAL_UINT64(9, last_pid);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_SIGNAL_INTERRUPT, last_signal);
}

void test_kill_prints_named_error(void)
{
    fake_result = PROCESS_SIGNAL_PROCESS_NOT_FOUND;
    char * argv[] = {"kill", "-TERM", "123"};

    TEST_ASSERT_EQUAL_INT(1, kill_main_under_test(3, argv));
    TEST_ASSERT_EQUAL_STRING("kill: error PROCESS_SIGNAL_PROCESS_NOT_FOUND (-2)\n", output);
}
