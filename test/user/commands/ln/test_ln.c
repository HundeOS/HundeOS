#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"

static IOStatus fake_result;
static IOLinkRequest last_link_request;
static char output[512];
static uint64_t output_position;

#define main ln_main_under_test
#include "../../../../user/commands/ln/main.c"
#include "../../../../user/lib/sys/io.c"

void setUp(void)
{
    fake_result = IO_OK;
    last_link_request = (IOLinkRequest){0};
    output_position = 0;
    output[0] = '\0';
}

void tearDown(void)
{
}

IOStatus IOClient__link(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    last_link_request = (IOLinkRequest){.source_path = *source_path, .target_path = *target_path};
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

void test_ln_links_two_paths(void)
{
    char * argv[] = {"ln", "src.txt", "/dst.txt"};

    TEST_ASSERT_EQUAL_INT(0, ln_main_under_test(3, argv));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/src.txt"), last_link_request.source_path);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dst.txt"), last_link_request.target_path);
}

void test_ln_prints_named_error(void)
{
    fake_result = IO_OPERATION_NOT_SUPPORTED;
    char * argv[] = {"ln", "/a", "/b"};

    TEST_ASSERT_EQUAL_INT(1, ln_main_under_test(3, argv));
    TEST_ASSERT_EQUAL_STRING("ln: error IO_OPERATION_NOT_SUPPORTED (-7)\n", output);
}

IOHandle IOClient__open(ConstBStringPtr path, Flags flags)
{
    if (!BString__starts_with(path, &bstr(IO_SOCKET_SCHEME)))
    {
        return IO_PATH_UNMANAGED;
    }
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}

IOReply IOClient__path_request(IORequestOperation operation, ConstBStringPtr path, Flags flags, uint64_t offset)
{
    return IOReply__new(IO_PATH_UNMANAGED, 0);
}

IOHandle IOClient__accept(IOHandle handle)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}

IOStatus IOClient__close(IOHandle handle)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return IO_UNKNOWN_HANDLE;
}

ByteCount IOClient__read(IOHandle handle, ChannelDataPtr data, size_t size)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}

ByteCount IOClient__write(IOHandle handle, ConstChannelDataPtr data, size_t size)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}
