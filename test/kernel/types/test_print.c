#include "unity.h"
#include "../../../kernel/print.h"

void BasicIO__reset_output();
const char * BasicIO__output();

void setUp(void)
{
    BasicIO__reset_output();
}

void tearDown(void)
{
}

void test_k_printf_returns_zero_for_null_format(void)
{
    TEST_ASSERT_EQUAL_INT(0, k_printf(NULL));
    TEST_ASSERT_EQUAL_STRING("", BasicIO__output());
}

void test_k_printf_prints_null_string_argument(void)
{
    int count = k_printf("%s", NULL);

    TEST_ASSERT_EQUAL_INT(6, count);
    TEST_ASSERT_EQUAL_STRING("(null)", BasicIO__output());
}

void test_k_sprintf_impl_returns_zero_for_null_format(void)
{
    char output[16] = "x";

    TEST_ASSERT_EQUAL_INT(0, k_sprintf(output, NULL));
    TEST_ASSERT_EQUAL_STRING("", output);
}

void test_k_sprintf_prints_null_string_argument(void)
{
    char output[16] = {0};

    TEST_ASSERT_EQUAL_INT(6, k_sprintf(output, "%s", NULL));
    TEST_ASSERT_EQUAL_STRING("(null)", output);
}
