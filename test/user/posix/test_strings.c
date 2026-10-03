#include "unity.h"

#include "../../../user/lib/newlib/string.c"
#include "../../../user/lib/newlib/stdlib.c"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_strcasecmp_ignores_ascii_case(void)
{
    TEST_ASSERT_EQUAL(0, strcasecmp("SigTerm", "SIGTERM"));
    TEST_ASSERT_TRUE(strcasecmp("SIGKILL", "SIGTERM") < 0);
    TEST_ASSERT_TRUE(strcasecmp("SIGTERM", "SIGKILL") > 0);
}

void test_strncasecmp_stops_at_requested_length(void)
{
    TEST_ASSERT_EQUAL(0, strncasecmp("SIGTERM", "sigtest", 4));
    TEST_ASSERT_EQUAL(0, strncasecmp("SIGTERM", "sigtest", 5));
    TEST_ASSERT_TRUE(strncasecmp("SIGTERM", "sigtest", 6) < 0);
    TEST_ASSERT_EQUAL(0, strncasecmp("different", "VALUES", 0));
}

void test_strdup_returns_an_independent_copy(void)
{
    char source[] = "shell";

    char * copy = strdup(source);

    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING("shell", copy);
    TEST_ASSERT_NOT_EQUAL(source, copy);
    source[0] = 'S';
    TEST_ASSERT_EQUAL_STRING("shell", copy);
    free(copy);
}

void test_strdup_copies_an_empty_string(void)
{
    char * copy = strdup("");

    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING("", copy);
    free(copy);
}

void test_srand_restarts_the_deterministic_rand_sequence(void)
{
    int first_sequence[4];

    srand(1234);
    for (size_t index = 0; index < 4; index++)
    {
        first_sequence[index] = rand();
    }

    srand(1234);
    for (size_t index = 0; index < 4; index++)
    {
        TEST_ASSERT_EQUAL_INT(first_sequence[index], rand());
    }
}

void test_rand_returns_values_within_its_documented_range(void)
{
    srand(0);

    for (size_t index = 0; index < 128; index++)
    {
        int value = rand();
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, value);
        TEST_ASSERT_LESS_OR_EQUAL_INT(RAND_MAX, value);
    }
}
