#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"

ByteCount write(IOHandle handle, char * buf, size_t size)
{
    return (ByteCount) size;
}

#include "../../../../user/sys/print.c"

static int format(char * buffer, const char * fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int count = sprintf_impl(buffer, fmt, args);
    va_end(args);
    return count;
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_sprintf_formats_hex_without_global_digit_pointer(void)
{
    char buffer[64];

    int count = format(buffer, "magic=0x%x", 0x74726976U);

    TEST_ASSERT_EQUAL_STRING("magic=0x74726976", buffer);
    TEST_ASSERT_EQUAL_INT(16, count);
}

void test_sprintf_formats_pointer_hex_prefix(void)
{
    char buffer[64];

    int count = format(buffer, "ptr=%p", 0xFEU);

    TEST_ASSERT_EQUAL_STRING("ptr=0xFE", buffer);
    TEST_ASSERT_EQUAL_INT(8, count);
}
