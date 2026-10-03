#include "unity.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char * input;
static size_t input_position;

static int test_getc(FILE * stream)
{
    if (input[input_position] == '\0')
    {
        return EOF;
    }
    return (unsigned char)input[input_position++];
}

#define getc test_getc
#define fgets test_fgets
#include "../../../user/lib/newlib/fgets.c"
#undef fgets
#undef getc

void setUp(void)
{
    input = "";
    input_position = 0;
    errno = 0;
}

void tearDown(void)
{
}

void test_fgets_reads_through_the_newline(void)
{
    char line[16];
    input = "first\nsecond";

    TEST_ASSERT_EQUAL_PTR(line, test_fgets(line, sizeof(line), (FILE *)1));
    TEST_ASSERT_EQUAL_STRING("first\n", line);
    TEST_ASSERT_EQUAL_PTR(line, test_fgets(line, sizeof(line), (FILE *)1));
    TEST_ASSERT_EQUAL_STRING("second", line);
}

void test_fgets_limits_output_and_leaves_remaining_input(void)
{
    char line[4];
    input = "abcdef";

    TEST_ASSERT_EQUAL_PTR(line, test_fgets(line, sizeof(line), (FILE *)1));
    TEST_ASSERT_EQUAL_STRING("abc", line);
    TEST_ASSERT_EQUAL_PTR(line, test_fgets(line, sizeof(line), (FILE *)1));
    TEST_ASSERT_EQUAL_STRING("def", line);
}

void test_fgets_returns_null_at_immediate_eof(void)
{
    char line[4] = "old";

    TEST_ASSERT_NULL(test_fgets(line, sizeof(line), (FILE *)1));
    TEST_ASSERT_EQUAL_STRING("old", line);
}

void test_fgets_rejects_invalid_arguments(void)
{
    char line[4];

    TEST_ASSERT_NULL(test_fgets(line, 0, (FILE *)1));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}
