#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "unity.h"

#include "../../../../common/types/bstring.h"
#include "../../../../common_includes/syscalls/io.h"

static const char * next_input;
static ByteCount next_input_size;
static BString printed_output;

Result(ByteCount) IO__read_bytes(IOHandle handle, char * buf, size_t size)
{
    ByteCount copied = 0;

    for (; copied < next_input_size && copied < (ByteCount)size; copied++)
    {
        buf[copied] = next_input[copied];
    }

    return Success(ByteCount, copied);
}

void IO__print(ConstBStringPtr text)
{
    printed_output = BString__append(&printed_output, text);
}

#include "../../../../user/lib/sys/console.c"

void setUp(void)
{
    next_input = "";
    next_input_size = 0;
    printed_output = empty_bstr();
}

void tearDown(void)
{
}

void test_console_read_returns_bstring(void)
{
    next_input = "abc";
    next_input_size = 3;

    BString input = Console__read();

    TEST_ASSERT_EQUAL_BSTRING(bstr("abc"), input);
    TEST_ASSERT_EQUAL_INT64(3, input.len);
}

void test_console_read_line_returns_bstring_until_newline(void)
{
    next_input = "hello\n";
    next_input_size = 6;

    BString line = Console__read_line(false);

    TEST_ASSERT_EQUAL_BSTRING(bstr("hello"), line);
    TEST_ASSERT_EQUAL_INT64(5, line.len);
}

void test_console_read_line_handles_backspace_and_echo(void)
{
    next_input = "ab\bcd\n";
    next_input_size = 6;

    BString line = Console__read_line(true);

    TEST_ASSERT_EQUAL_BSTRING(bstr("acd"), line);
    TEST_ASSERT_EQUAL_BSTRING(bstr("ab\b \bcd"), printed_output);
}

void test_console_read_does_not_write_past_one_byte_buffer(void)
{
    next_input = "x";
    next_input_size = 1;

    char buffer[2] = {'?', '!'};
    ByteCount read_count = console_read(buffer, 1);

    TEST_ASSERT_EQUAL_INT64(1, read_count);
    TEST_ASSERT_EQUAL_CHAR('x', buffer[0]);
    TEST_ASSERT_EQUAL_CHAR('!', buffer[1]);
}
