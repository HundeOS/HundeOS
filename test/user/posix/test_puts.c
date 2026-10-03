#include "unity.h"

#include <stdio.h>
#include <string.h>

static char output[32];
static size_t output_length;
static int fputs_result;
static int putchar_result;

static int test_fputs(const char * text, FILE * stream)
{
    if (fputs_result == EOF)
    {
        return EOF;
    }
    size_t length = strlen(text);
    memcpy(output + output_length, text, length);
    output_length += length;
    return 0;
}

static int test_putchar(int character)
{
    if (putchar_result == EOF)
    {
        return EOF;
    }
    output[output_length++] = (char)character;
    return character;
}

#define fputs test_fputs
#define putchar test_putchar
#define puts test_puts
#include "../../../user/lib/newlib/puts.c"
#undef puts
#undef putchar
#undef fputs

void setUp(void)
{
    memset(output, 0, sizeof(output));
    output_length = 0;
    fputs_result = 0;
    putchar_result = 0;
}

void tearDown(void)
{
}

void test_puts_writes_text_followed_by_a_newline(void)
{
    TEST_ASSERT_EQUAL_INT(0, test_puts("hello"));
    TEST_ASSERT_EQUAL_size_t(6, output_length);
    TEST_ASSERT_EQUAL_MEMORY("hello\n", output, output_length);
}

void test_puts_writes_a_newline_for_an_empty_string(void)
{
    TEST_ASSERT_EQUAL_INT(0, test_puts(""));
    TEST_ASSERT_EQUAL_size_t(1, output_length);
    TEST_ASSERT_EQUAL_CHAR('\n', output[0]);
}

void test_puts_returns_eof_when_writing_text_fails(void)
{
    fputs_result = EOF;

    TEST_ASSERT_EQUAL_INT(EOF, test_puts("hello"));
    TEST_ASSERT_EQUAL_size_t(0, output_length);
}

void test_puts_returns_eof_when_writing_the_newline_fails(void)
{
    putchar_result = EOF;

    TEST_ASSERT_EQUAL_INT(EOF, test_puts("hello"));
    TEST_ASSERT_EQUAL_size_t(5, output_length);
    TEST_ASSERT_EQUAL_MEMORY("hello", output, output_length);
}
