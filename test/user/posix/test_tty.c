#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"

#include "../../../user/lib/newlib/posix_io.h"

Optional(BString) PosixIO__terminal_path(int descriptor)
{
    if (descriptor == 7)
    {
        return Some(BString, bstr(":tty:"));
    }
    errno = descriptor == 8 ? ENOTTY : EBADF;
    return None(BString);
}

#include "../../../user/lib/newlib/tty.c"

void setUp(void)
{
    errno = 0;
}

void tearDown(void)
{
}

void test_ttyname_and_isatty_return_the_terminal_identity(void)
{
    TEST_ASSERT_EQUAL_STRING(":tty:", ttyname(7));
    TEST_ASSERT_EQUAL_INT(1, isatty(7));
}

void test_ttyname_and_isatty_reject_a_nonterminal_descriptor(void)
{
    TEST_ASSERT_NULL(ttyname(8));
    TEST_ASSERT_EQUAL_INT(ENOTTY, errno);
    TEST_ASSERT_EQUAL_INT(0, isatty(8));
    TEST_ASSERT_EQUAL_INT(ENOTTY, errno);
}

void test_ttyname_and_isatty_reject_an_invalid_descriptor(void)
{
    TEST_ASSERT_NULL(ttyname(99));
    TEST_ASSERT_EQUAL_INT(EBADF, errno);
    TEST_ASSERT_EQUAL_INT(0, isatty(99));
    TEST_ASSERT_EQUAL_INT(EBADF, errno);
}
