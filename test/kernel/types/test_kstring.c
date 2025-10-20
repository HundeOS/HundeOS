#include "unity.h"
#include "../../../kernel/types/kstring.h"
#include "../../../kernel/print.h"


#define TEST_KSTRING_EQUALS(expected, actual) \
    bool equal = KString__equals(&expected, &actual); \
    if (!equal) \
    { \
        k_printf("expected=\"%s\" actual=\"%s\"\n", KString__cstr(&expected), KString__cstr(&actual));\
    } \
    TEST_ASSERT_TRUE(equal); \

void setUp(void)
{

}

void tearDown(void)
{

}


void test_kstring_equals_when_equals(void)
{
    KString str1 = kstring("hello");
    KString str2 = kstring("hello");

    TEST_ASSERT_TRUE(KString__equals(&str1, &str2));
}

void test_kstring_equals_when_not_equal(void)
{
    KString str1 = kstring("hello");
    KString str2 = kstring("goodbye");

    TEST_ASSERT_FALSE(KString__equals(&str1, &str2));
}

void test_kstring_equals_when_not_equal_same_length(void)
{
    KString str1 = kstring("hello");
    KString str2 = kstring("hella");

    TEST_ASSERT_FALSE(KString__equals(&str1, &str2));
}

void test_kstring_sprintf(void)
{
    KString format = kstring("hello %i %p");
    KString expected = kstring("hello 10 0xFE");

    KString formatted = KString__sprintf(&format, 10, 0xFE);
    TEST_KSTRING_EQUALS(expected, formatted);
}

void test_kstring_len(void)
{
    KString str = kstring("123456789");

    TEST_ASSERT_EQUAL_UINT(9, str.len);
}

void test_kstring_append(void)
{
    KString str1 = kstring("12345");
    KString str2 = kstring("6789");
    KString expected = kstring("123456789");
    KString appended = KString__append(&str1, &str2);

    TEST_KSTRING_EQUALS(expected, appended);
}
