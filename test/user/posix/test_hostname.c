#include "unity.h"

#include <errno.h>
#include <unistd.h>

#include "../../../user/lib/newlib/hostname.c"

void setUp(void)
{
    errno = 0;
}

void tearDown(void)
{
}

void test_gethostname_returns_the_fixed_hundeos_name(void)
{
    char name[32] = {0};

    TEST_ASSERT_EQUAL_INT(0, gethostname(name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("hundeos", name);
}

void test_gethostname_accepts_exact_capacity_including_the_terminator(void)
{
    char name[8] = {0};

    TEST_ASSERT_EQUAL_INT(0, gethostname(name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("hundeos", name);
}

void test_gethostname_rejects_an_undersized_buffer_without_modifying_it(void)
{
    char name[7] = "keep";

    TEST_ASSERT_EQUAL_INT(-1, gethostname(name, sizeof(name)));
    TEST_ASSERT_EQUAL_INT(ENAMETOOLONG, errno);
    TEST_ASSERT_EQUAL_STRING("keep", name);
}

void test_gethostname_rejects_a_missing_buffer(void)
{
    TEST_ASSERT_EQUAL_INT(-1, gethostname(NULL, 0));
    TEST_ASSERT_EQUAL_INT(EFAULT, errno);
}
