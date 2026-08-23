#include "unity.h"
#include "../../../kernel/types/bstring.h"
#include "../../../kernel/types/bstring_additional_types.h"
#include "../../../kernel/print.h"

BString hello = BSTRING_NEW("hello");
BString world = BSTRING_NEW("world");

#define TEST_KSTRING_EQUALS(expected, actual) \
    bool equal = BString__equals(&expected, &actual); \
    if (!equal) \
    { \
        k_printf("expected=\"%s\" actual=\"%s\"\n", BString__cstr(&expected), BString__cstr(&actual));\
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
    BString str = bstring("hello/world");
    BStringArray strs = BString__split(&str, '/');

    TEST_ASSERT_EQUAL_INT(2, BStringArray__len(&strs));
    BStringPtr str1 = Unwrap(BStringPtr, BStringArray__ptr_at(&strs, 0), (BStringPtr){});
    BStringPtr str2 = Unwrap(BStringPtr, BStringArray__ptr_at(&strs, 1), (BStringPtr){});

    TEST_ASSERT_TRUE(BString__equals(str1, &hello));
    TEST_ASSERT_TRUE(BString__equals(str2, &world));
}

void test_kstring_split_preceeding_delimiter(void)
{
    BString str = bstring("/hello/world");
    BStringArray strs = BString__split(&str, '/');

    TEST_ASSERT_EQUAL_INT(2, BStringArray__len(&strs));
    BStringPtr str1 = Unwrap(BStringPtr, BStringArray__ptr_at(&strs, 0), (BStringPtr){});
    BStringPtr str2 = Unwrap(BStringPtr, BStringArray__ptr_at(&strs, 1), (BStringPtr){});

    TEST_ASSERT_TRUE(BString__equals(str1, &hello));
    TEST_ASSERT_TRUE(BString__equals(str2, &world));
}

void test_kstring_split_trailing_delimiters(void)
{
    BString str = bstring("/hello/world//");
    BStringArray strs = BString__split(&str, '/');

    TEST_ASSERT_EQUAL_INT(2, BStringArray__len(&strs));
    BStringPtr str1 = Unwrap(BStringPtr, BStringArray__ptr_at(&strs, 0), (BStringPtr){});
    BStringPtr str2 = Unwrap(BStringPtr, BStringArray__ptr_at(&strs, 1), (BStringPtr){});

    TEST_ASSERT_TRUE(BString__equals(str1, &hello));
    TEST_ASSERT_TRUE(BString__equals(str2, &world));
}

void test_kstring_split_double_delimiter(void)
{
    BString str = bstring("/hello//world");
    BStringArray strs = BString__split(&str, '/');

    TEST_ASSERT_EQUAL_INT(2, BStringArray__len(&strs));
    BStringPtr str1 = Unwrap(BStringPtr, BStringArray__ptr_at(&strs, 0), (BStringPtr){});
    BStringPtr str2 = Unwrap(BStringPtr, BStringArray__ptr_at(&strs, 1), (BStringPtr){});

    TEST_ASSERT_TRUE(BString__equals(str1, &hello));
    TEST_ASSERT_TRUE(BString__equals(str2, &world));
}

void test_bstring_equals_when_equals(void)
{
    BString str1 = bstring("hello");
    BString str2 = bstring("hello");

    TEST_ASSERT_TRUE(BString__equals(&str1, &str2));
}

void test_bstring_equals_when_not_equal(void)
{
    BString str1 = bstring("hello");
    BString str2 = bstring("goodbye");

    TEST_ASSERT_FALSE(BString__equals(&str1, &str2));
}

void test_bstring_equals_when_not_equal_same_length(void)
{
    BString str1 = bstring("hello");
    BString str2 = bstring("hella");

    TEST_ASSERT_FALSE(BString__equals(&str1, &str2));
}

BString delimited_string = BSTRING_NEW("hello/world/how/are/you");
BString rejoined_string = BSTRING_NEW("hello:world:how:are:you");
void test_kstring_join(void)
{
    BStringArray strs = BString__split(&delimited_string, '/');

    BString joined = BString__join(&strs, ':');
    TEST_KSTRING_EQUALS(rejoined_string, joined);
}

void test_kstring_sprintf(void)
{
    BString format = bstring("hello %i %p");
    BString expected = bstring("hello 10 0xfe");

    BString formatted = BString__sprintf(&format, 10, 0xFE);
    TEST_KSTRING_EQUALS(expected, formatted);
}

void test_kstring_len(void)
{
    BString str = bstring("123456789");

    TEST_ASSERT_EQUAL_UINT(9, BString__length(&str));
}

void test_kstring_char_at_returns_character_when_in_bounds(void)
{
    BString str = bstring("hello");

    match_optional(char, BString__char_at(&str, 1), some(ch) { TEST_ASSERT_EQUAL_CHAR('e', ch); }, none() { TEST_FAIL(); })
}

void test_kstring_char_at_returns_none_when_out_of_bounds(void)
{
    BString str = bstring("hello");

    match_optional(char, BString__char_at(&str, 5), some(ch) { TEST_FAIL(); }, none() { TEST_ASSERT_TRUE(true); })
}

void test_kstring_uppercase(void)
{
    BString str = bstring("Hello-123.txt");
    BString expected = bstring("HELLO-123.TXT");

    BString upper = BString__uppercase(&str);
    TEST_KSTRING_EQUALS(expected, upper);
}

void test_kstring_append(void)
{
    BString str1 = bstring("12345");
    BString str2 = bstring("6789");
    BString expected = bstring("123456789");
    BString appended = BString__append(&str1, &str2);

    TEST_KSTRING_EQUALS(expected, appended);
}

void test_kstring_copy_to_copies_and_null_terminates(void)
{
    BString str = bstring("hello");
    char target[8] = {0};

    StringPos copied = BString__copy_to(&str, target, sizeof(target));

    TEST_ASSERT_EQUAL_UINT64(5, copied);
    TEST_ASSERT_EQUAL_STRING("hello", target);
}

void test_kstring_copy_to_truncates_to_leave_null_terminator(void)
{
    BString str = bstring("hello");
    char target[4] = {0};

    StringPos copied = BString__copy_to(&str, target, sizeof(target));

    TEST_ASSERT_EQUAL_UINT64(3, copied);
    TEST_ASSERT_EQUAL_STRING("hel", target);
}

void test_kstring_copy_to_zero_sized_target_copies_nothing(void)
{
    BString str = bstring("hello");
    char target[1] = {'x'};

    StringPos copied = BString__copy_to(&str, target, 0);

    TEST_ASSERT_EQUAL_UINT64(0, copied);
    TEST_ASSERT_EQUAL_CHAR('x', target[0]);
}

void test_kstring_starts_with_when_starts_with(void)
{
    BString match = bstring("12345");
    BString full = bstring("123456789");

    TEST_ASSERT_TRUE(BString__starts_with(&full, &match));
}

void test_kstring_starts_with_when_no_match(void)
{
    BString no_match = bstring("789");
    BString full = bstring("123456789");

    TEST_ASSERT_FALSE(BString__starts_with(&full, &no_match));
}

void test_kstring_starts_with_when_comparing_to_empty(void)
{
    BString empty = bstring("");
    BString full = bstring("123456789");

    TEST_ASSERT_FALSE(BString__starts_with(&full, &empty));
}

void test_kstring_starts_with_when_empty(void)
{
    BString compare = bstring("9876");
    BString empty = bstring("");

    TEST_ASSERT_FALSE(BString__starts_with(&empty, &compare));
}
