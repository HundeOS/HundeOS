#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "unity.h"

#include "../../../../user/lib/sys/syscall.h"
#include "../../../../common_includes/syscalls/io.h"

static IOStatus fake_result;
static SystemCall last_call;
static Anything last_arg1;
static IOLinkRequest last_link_request;
static char output[512];
static uint64_t output_position;

#define main ln_main_under_test
#include "../../../../user/commands/ln/main.c"
#include "../../../../user/lib/sys/io.c"

void setUp(void)
{
    fake_result = IO_OK;
    last_call = 0;
    last_arg1 = 0;
    last_link_request = (IOLinkRequest){0};
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
    last_link_request = *((IOLinkRequestPtr)arg2);
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

void test_ln_calls_link_syscall_for_two_paths(void)
{
    char * argv[] = {"ln", "src.txt", "/dst.txt"};

    TEST_ASSERT_EQUAL_INT(0, ln_main_under_test(3, argv));
    TEST_ASSERT_EQUAL(IO_CALL, last_call);
    TEST_ASSERT_EQUAL(IO_LINK, last_arg1);
    TEST_ASSERT_EQUAL_STRING("/src.txt", BString__cstr(&last_link_request.source_path));
    TEST_ASSERT_EQUAL_STRING("/dst.txt", BString__cstr(&last_link_request.target_path));
}

void test_ln_prints_named_error(void)
{
    fake_result = IO_OPERATION_NOT_SUPPORTED;
    char * argv[] = {"ln", "/a", "/b"};

    TEST_ASSERT_EQUAL_INT(1, ln_main_under_test(3, argv));
    TEST_ASSERT_EQUAL_STRING("ln: error IO_OPERATION_NOT_SUPPORTED (-7)\n", output);
}
