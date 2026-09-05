#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "unity.h"

#include "../../../../common/types/bstring.h"
#include "../../../../common_includes/syscalls/io.h"

static char output[256];
static size_t output_position;

ByteCount IO__write(IOHandle handle, ConstBStringPtr text)
{
    const char * buf = BString__cstr(text);
    size_t size = (size_t) BString__length(text);
    for (size_t i = 0; i < size && output_position + 1 < sizeof(output); i++)
    {
        output[output_position++] = buf[i];
    }
    output[output_position] = '\0';
    return (ByteCount) size;
}

#include "../../../../user/lib/sys/print.c"

void setUp(void)
{
    output_position = 0;
    output[0] = '\0';
}

void tearDown(void)
{
}

void test_sprintf_formats_hex_without_global_digit_pointer(void)
{
    BString format = bstr("magic=0x%x");

    int count = IO__printf(&bstr("magic=0x%x"), 0x74726976U);

    TEST_ASSERT_EQUAL_STRING("magic=0x74726976", output);
    TEST_ASSERT_EQUAL_INT(16, count);
}

void test_sprintf_formats_pointer_hex_prefix(void)
{

    int count = IO__printf(&bstr("ptr=%p"), (uintptr_t) 0xFEU);

    TEST_ASSERT_EQUAL_STRING("ptr=0xfe", output);
    TEST_ASSERT_EQUAL_INT(8, count);
}
