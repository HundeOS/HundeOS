#include "unity.h"
#include "../../../kernel/types/kstring.h"
#include "../../../kernel/types/kstring_additional_types.h"
#include "../../../kernel/print.h"

KString hello = KSTRING_NEW("hello");
KString world = KSTRING_NEW("world");

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


void test_kstring_split(void)
{
    KString str = kstring("hello/world");
    KStringArray strs = KString__split(&str, '/');

    TEST_ASSERT_EQUAL_INT(2, KStringArray__len(&strs));
    KStringPtr str1 = Unwrap(KStringPtr, KStringArray__ptr_at(&strs, 0), (KStringPtr){});
    KStringPtr str2 = Unwrap(KStringPtr, KStringArray__ptr_at(&strs, 1), (KStringPtr){});

    TEST_ASSERT_TRUE(KString__equals(str1, &hello));
    TEST_ASSERT_TRUE(KString__equals(str2, &world));
}

void test_kstring_split_preceeding_delimiter(void)
{
    KString str = kstring("/hello/world");
    KStringArray strs = KString__split(&str, '/');

    TEST_ASSERT_EQUAL_INT(2, KStringArray__len(&strs));
    KStringPtr str1 = Unwrap(KStringPtr, KStringArray__ptr_at(&strs, 0), (KStringPtr){});
    KStringPtr str2 = Unwrap(KStringPtr, KStringArray__ptr_at(&strs, 1), (KStringPtr){});

    TEST_ASSERT_TRUE(KString__equals(str1, &hello));
    TEST_ASSERT_TRUE(KString__equals(str2, &world));
}

void test_kstring_split_trailing_delimiters(void)
{
    KString str = kstring("/hello/world//");
    KStringArray strs = KString__split(&str, '/');

    TEST_ASSERT_EQUAL_INT(2, KStringArray__len(&strs));
    KStringPtr str1 = Unwrap(KStringPtr, KStringArray__ptr_at(&strs, 0), (KStringPtr){});
    KStringPtr str2 = Unwrap(KStringPtr, KStringArray__ptr_at(&strs, 1), (KStringPtr){});

    TEST_ASSERT_TRUE(KString__equals(str1, &hello));
    TEST_ASSERT_TRUE(KString__equals(str2, &world));
}

void test_kstring_split_double_delimiter(void)
{
    KString str = kstring("/hello//world");
    KStringArray strs = KString__split(&str, '/');

    TEST_ASSERT_EQUAL_INT(2, KStringArray__len(&strs));
    KStringPtr str1 = Unwrap(KStringPtr, KStringArray__ptr_at(&strs, 0), (KStringPtr){});
    KStringPtr str2 = Unwrap(KStringPtr, KStringArray__ptr_at(&strs, 1), (KStringPtr){});

    TEST_ASSERT_TRUE(KString__equals(str1, &hello));
    TEST_ASSERT_TRUE(KString__equals(str2, &world));
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

KString delimited_string = KSTRING_NEW("hello/world/how/are/you");
KString rejoined_string = KSTRING_NEW("hello:world:how:are:you");
void test_kstring_join(void)
{
    KStringArray strs = KString__split(&delimited_string, '/');

    KString joined = KString__join(&strs, ':');
    TEST_KSTRING_EQUALS(rejoined_string, joined);
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

    TEST_ASSERT_EQUAL_UINT(9, KString__length(&str));
}

void test_kstring_char_at_returns_character_when_in_bounds(void)
{
    KString str = kstring("hello");

    Match(char, KString__char_at(&str, 1))
        IsSome(ch, TEST_ASSERT_EQUAL_CHAR('e', ch))
        IsNone(TEST_FAIL())
    End
}

void test_kstring_char_at_returns_none_when_out_of_bounds(void)
{
    KString str = kstring("hello");

    Match(char, KString__char_at(&str, 5))
        IsSome(ch, TEST_FAIL())
        IsNone(TEST_ASSERT_TRUE(true))
    End
}

void test_kstring_uppercase(void)
{
    KString str = kstring("Hello-123.txt");
    KString expected = kstring("HELLO-123.TXT");

    KString upper = KString__uppercase(&str);
    TEST_KSTRING_EQUALS(expected, upper);
}

void test_kstring_append(void)
{
    KString str1 = kstring("12345");
    KString str2 = kstring("6789");
    KString expected = kstring("123456789");
    KString appended = KString__append(&str1, &str2);

    TEST_KSTRING_EQUALS(expected, appended);
}

void test_kstring_copy_to_copies_and_null_terminates(void)
{
    KString str = kstring("hello");
    char target[8] = {0};

    StringPos copied = KString__copy_to(&str, target, sizeof(target));

    TEST_ASSERT_EQUAL_UINT64(5, copied);
    TEST_ASSERT_EQUAL_STRING("hello", target);
}

void test_kstring_copy_to_truncates_to_leave_null_terminator(void)
{
    KString str = kstring("hello");
    char target[4] = {0};

    StringPos copied = KString__copy_to(&str, target, sizeof(target));

    TEST_ASSERT_EQUAL_UINT64(3, copied);
    TEST_ASSERT_EQUAL_STRING("hel", target);
}

void test_kstring_copy_to_zero_sized_target_copies_nothing(void)
{
    KString str = kstring("hello");
    char target[1] = {'x'};

    StringPos copied = KString__copy_to(&str, target, 0);

    TEST_ASSERT_EQUAL_UINT64(0, copied);
    TEST_ASSERT_EQUAL_CHAR('x', target[0]);
}

void test_kstring_starts_with_when_starts_with(void)
{
    KString match = kstring("12345");
    KString full = kstring("123456789");

    TEST_ASSERT_TRUE(KString__starts_with(&full, &match));
}

void test_kstring_starts_with_when_no_match(void)
{
    KString no_match = kstring("789");
    KString full = kstring("123456789");

    TEST_ASSERT_FALSE(KString__starts_with(&full, &no_match));
}

void test_kstring_starts_with_when_comparing_to_empty(void)
{
    KString empty = kstring("");
    KString full = kstring("123456789");

    TEST_ASSERT_FALSE(KString__starts_with(&full, &empty));
}

void test_kstring_starts_with_when_empty(void)
{
    KString compare = kstring("9876");
    KString empty = kstring("");

    TEST_ASSERT_FALSE(KString__starts_with(&empty, &compare));
}
