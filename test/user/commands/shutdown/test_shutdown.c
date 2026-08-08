#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/calls.h"

static SystemCallResult fake_result;
static char output[256];
static uint64_t output_position;
static uint64_t shutdown_call_count;

#define main shutdown_main_under_test
#include "../../../../user/commands/shutdown/main.c"

void setUp(void)
{
    fake_result = SYSCALL_OK;
    output_position = 0;
    output[0] = '\0';
    shutdown_call_count = 0;
}

void tearDown(void)
{
}

SystemCallResult shutdown_system(void)
{
    shutdown_call_count++;
    return fake_result;
}

void print(const char * str)
{
    while (*str != '\0' && output_position + 1 < sizeof(output))
    {
        output[output_position++] = *str++;
    }
    output[output_position] = '\0';
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

void test_shutdown_invokes_shutdown_system(void)
{
    char * argv[] = { "shutdown" };

    TEST_ASSERT_EQUAL_INT(0, shutdown_main_under_test(1, argv));
    TEST_ASSERT_EQUAL_UINT64(1, shutdown_call_count);
}

void test_shutdown_prints_error_when_shutdown_fails(void)
{
    char * argv[] = { "shutdown" };
    fake_result = SYSCALL_IO_BLOCK;

    TEST_ASSERT_EQUAL_INT(1, shutdown_main_under_test(1, argv));
    TEST_ASSERT_EQUAL_STRING("shutdown: error -2\n", output);
}
