#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/calls.h"

static SystemCallResult fake_result;
static char output[256];
static uint64_t output_position;
static uint64_t panic_call_count;

#define PANIC_ENTRYPOINT panic_main_under_test
#include "../../../../user/commands/panic/main.c"

void setUp(void)
{
    fake_result = SYSCALL_OK;
    output_position = 0;
    output[0] = '\0';
    panic_call_count = 0;
}

void tearDown(void)
{
}

SystemCallResult panic_system(void)
{
    panic_call_count++;
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

void test_panic_invokes_panic_system(void)
{
    char * argv[] = {"panic"};

    TEST_ASSERT_EQUAL_INT(1, panic_main_under_test(1, argv));
    TEST_ASSERT_EQUAL_UINT64(1, panic_call_count);
    TEST_ASSERT_EQUAL_STRING("panic: syscall returned 0\n", output);
}

void test_panic_rejects_arguments(void)
{
    char * argv[] = {"panic", "now"};

    TEST_ASSERT_EQUAL_INT(1, panic_main_under_test(2, argv));
    TEST_ASSERT_EQUAL_UINT64(0, panic_call_count);
    TEST_ASSERT_EQUAL_STRING("panic: usage panic\n", output);
}
