#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "unity.h"

#include "../../../../user/sys/syscall.h"
#include "../../../../common_includes/syscalls/io.h"

static IOStatus fake_result;
static SystemCall last_call;
static Anything last_arg1;
static Anything last_arg2;
static Anything last_arg3;
static char output[512];
static uint64_t output_position;

#define main ln_main_under_test
#include "../../../../user/commands/ln/main.c"
#include "../../../../user/sys/io.c"

void setUp(void)
{
    fake_result = IO_OK;
    last_call = 0;
    last_arg1 = 0;
    last_arg2 = 0;
    last_arg3 = 0;
    output_position = 0;
    output[0] = '\0';
}

void tearDown(void)
{
}

Anything syscall(SystemCall call, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    last_call = call;
    last_arg1 = arg1;
    last_arg2 = arg2;
    last_arg3 = arg3;
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

void test_ln_calls_link_syscall_for_two_paths(void)
{
    char * argv[] = { "ln", "src.txt", "/dst.txt" };

    TEST_ASSERT_EQUAL_INT(0, ln_main_under_test(3, argv));
    TEST_ASSERT_EQUAL(IO_CALL, last_call);
    TEST_ASSERT_EQUAL(IO_LINK, last_arg1);
    TEST_ASSERT_EQUAL_STRING("/src.txt", (const char *) last_arg2);
    TEST_ASSERT_EQUAL_STRING("/dst.txt", (const char *) last_arg3);
}

void test_ln_prints_named_error(void)
{
    fake_result = IO_OPERATION_NOT_SUPPORTED;
    char * argv[] = { "ln", "/a", "/b" };

    TEST_ASSERT_EQUAL_INT(1, ln_main_under_test(3, argv));
    TEST_ASSERT_EQUAL_STRING("ln: error IO_OPERATION_NOT_SUPPORTED (-7)\n", output);
}
