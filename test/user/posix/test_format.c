#include "unity.h"

#include <stdarg.h>

#undef snprintf
#undef sprintf
#undef vsnprintf
#undef vsprintf

#include "../../../user/lib/newlib/format.c"

static int format_with_vsnprintf(char * buffer, size_t size, const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

static int format_with_vsprintf(char * buffer, const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsprintf(buffer, format, arguments);
    va_end(arguments);
    return result;
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_sprintf_formats_into_the_destination(void)
{
    char buffer[32];

    int length = sprintf(buffer, "%s-%d", "job", 12);

    TEST_ASSERT_EQUAL_INT(6, length);
    TEST_ASSERT_EQUAL_STRING("job-12", buffer);
}

void test_vsnprintf_reports_the_untruncated_length(void)
{
    char buffer[5];

    int length = format_with_vsnprintf(buffer, sizeof(buffer), "%s", "abcdef");

    TEST_ASSERT_EQUAL_INT(6, length);
    TEST_ASSERT_EQUAL_STRING("abcd", buffer);
}

void test_vsnprintf_accepts_a_null_destination_for_a_zero_sized_write(void)
{
    TEST_ASSERT_EQUAL_INT(6, format_with_vsnprintf(NULL, 0, "%s", "abcdef"));
}

void test_vsprintf_formats_into_the_destination(void)
{
    char buffer[32];

    int length = format_with_vsprintf(buffer, "%s-%u", "job", 7U);

    TEST_ASSERT_EQUAL_INT(5, length);
    TEST_ASSERT_EQUAL_STRING("job-7", buffer);
}
