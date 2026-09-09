#include "unity.h"
#include "../../../kernel/types/bstring.h"
#include "../../../kernel/types/bstring_additional_types.h"
#include "../../../kernel/print.h"

BString hello = bstr("hello");
BString world = bstr("world");

#define TEST_KSTRING_EQUALS(expected, actual)                                                                          \
    bool equal = BString__equals(&expected, &actual);                                                                  \
    if (!equal)                                                                                                        \
    {                                                                                                                  \
        k_printf("expected=\"%s\" actual=\"%s\"\n", BString__cstr(&expected), BString__cstr(&actual));                 \
    }                                                                                                                  \
    TEST_ASSERT_TRUE(equal);

void setUp(void)
{
}

void tearDown(void)
{
}

void test_kstring_split(void)
{
    BString str = bstring("hello/world");
    BStringArray strs = BString__split(&bstr("hello/world"), '/');

    TEST_ASSERT_EQUAL_INT(2, BStringArray__len(&strs));
    BStringPtr str1 = unwrap_optional(BStringArray__ptr_at(&strs, 0), (BStringPtr){});
    BStringPtr str2 = unwrap_optional(BStringArray__ptr_at(&strs, 1), (BStringPtr){});

    TEST_ASSERT_TRUE(BString__equals(str1, &hello));
    TEST_ASSERT_TRUE(BString__equals(str2, &world));
}

void test_kstring_split_preceeding_delimiter(void)
{
    BString str = bstring("/hello/world");
    BStringArray strs = BString__split(&bstr("/hello/world"), '/');

    TEST_ASSERT_EQUAL_INT(2, BStringArray__len(&strs));
    BStringPtr str1 = unwrap_optional(BStringArray__ptr_at(&strs, 0), (BStringPtr){});
    BStringPtr str2 = unwrap_optional(BStringArray__ptr_at(&strs, 1), (BStringPtr){});

    TEST_ASSERT_TRUE(BString__equals(str1, &hello));
    TEST_ASSERT_TRUE(BString__equals(str2, &world));
}

void test_kstring_split_trailing_delimiters(void)
{
    BString str = bstring("/hello/world//");
    BStringArray strs = BString__split(&bstr("/hello/world//"), '/');

    TEST_ASSERT_EQUAL_INT(2, BStringArray__len(&strs));
    BStringPtr str1 = unwrap_optional(BStringArray__ptr_at(&strs, 0), (BStringPtr){});
    BStringPtr str2 = unwrap_optional(BStringArray__ptr_at(&strs, 1), (BStringPtr){});

    TEST_ASSERT_TRUE(BString__equals(str1, &hello));
    TEST_ASSERT_TRUE(BString__equals(str2, &world));
}

void test_kstring_split_double_delimiter(void)
{
    BString str = bstring("/hello//world");
    BStringArray strs = BString__split(&bstr("/hello//world"), '/');

    TEST_ASSERT_EQUAL_INT(2, BStringArray__len(&strs));
    BStringPtr str1 = unwrap_optional(BStringArray__ptr_at(&strs, 0), (BStringPtr){});
    BStringPtr str2 = unwrap_optional(BStringArray__ptr_at(&strs, 1), (BStringPtr){});

    TEST_ASSERT_TRUE(BString__equals(str1, &hello));
    TEST_ASSERT_TRUE(BString__equals(str2, &world));
}

void test_bstring_equals_when_equals(void)
{
    BString str1 = bstring("hello");
    BString str2 = bstring("hello");

    TEST_ASSERT_TRUE(BString__equals(&bstr("hello"), &bstr("hello")));
}

void test_bstring_equals_when_not_equal(void)
{
    BString str1 = bstring("hello");
    BString str2 = bstring("goodbye");

    TEST_ASSERT_FALSE(BString__equals(&bstr("12345"), &bstr("6789")));
}

void test_bstring_equals_when_not_equal_same_length(void)
{
    BString str1 = bstring("hello");
    BString str2 = bstring("hella");

    TEST_ASSERT_FALSE(BString__equals(&bstr("12345"), &bstr("6789")));
}

BString delimited_string = bstr("hello/world/how/are/you");
BString rejoined_string = bstr("hello:world:how:are:you");
void test_kstring_join(void)
{
    BStringArray strs = BString__split(&delimited_string, '/');

    BString joined = BString__join(&strs, ':');
    TEST_KSTRING_EQUALS(rejoined_string, joined);
}

void test_kstring_sprintf(void)
{
    BString expected = bstring("hello 10 0xfe");

    BString formatted = BString__sprintf(&bstr("hello %i %p"), 10, 0xFE);
    TEST_KSTRING_EQUALS(expected, formatted);
}

void test_kstring_len(void)
{
    BString str = bstring("123456789");

    TEST_ASSERT_EQUAL_UINT(9, BString__length(&bstr("123456789")));
}

void test_kstring_char_at_returns_character_when_in_bounds(void)
{
    BString str = bstring("hello");

    match_optional(
        BString__char_at(&bstr("hello"), 1),
        some(ch) { TEST_ASSERT_EQUAL_CHAR('e', ch); },
        none_test_fail()
    );
}

void test_kstring_char_at_returns_none_when_out_of_bounds(void)
{
    BString str = bstring("hello");

    match_optional(BString__char_at(&bstr("hello"), 5),
        some_test_fail(),
        none() { TEST_ASSERT_TRUE(true); }
    );
}

void test_kstring_uppercase(void)
{
    BString str = bstring("Hello-123.txt");
    BString expected = bstring("HELLO-123.TXT");

    BString upper = BString__uppercase(&bstr("Hello-123.txt"));
    TEST_KSTRING_EQUALS(expected, upper);
}

void test_kstring_append(void)
{
    BString expected = bstring("123456789");
    BString appended = BString__append(&bstr("12345"), &bstr("6789"));

    TEST_KSTRING_EQUALS(expected, appended);
}

void test_kstring_copy_to_copies_and_null_terminates(void)
{
    BString str = bstring("hello");
    char target[8] = {0};

    StringPos copied = BString__copy_to(&bstr("hello"), target, sizeof(target));

    TEST_ASSERT_EQUAL_UINT64(5, copied);
    TEST_ASSERT_EQUAL_STRING("hello", target);
}

void test_kstring_copy_to_truncates_to_leave_null_terminator(void)
{
    BString str = bstring("hello");
    char target[4] = {0};

    StringPos copied = BString__copy_to(&bstr("hello"), target, sizeof(target));

    TEST_ASSERT_EQUAL_UINT64(3, copied);
    TEST_ASSERT_EQUAL_STRING("hel", target);
}

void test_kstring_copy_to_zero_sized_target_copies_nothing(void)
{
    char target[1] = {'x'};

    StringPos copied = BString__copy_to(&bstr("hello"), target, 0);

    TEST_ASSERT_EQUAL_UINT64(0, copied);
    TEST_ASSERT_EQUAL_CHAR('x', target[0]);
}

void test_kstring_starts_with_when_starts_with(void)
{
    BString full = bstring("123456789");

    TEST_ASSERT_TRUE(BString__starts_with(&bstr("123456789"), &bstr("12345")));
}

void test_kstring_starts_with_when_no_match(void)
{
    BString full = bstring("123456789");

    TEST_ASSERT_FALSE(BString__starts_with(&bstr("123456789"), &bstr("789")));
}

void test_kstring_starts_with_when_comparing_to_empty(void)
{
    BString empty = bstring("");

    TEST_ASSERT_FALSE(BString__starts_with(&bstr("123456789"), &bstr("")));
}

void test_kstring_starts_with_when_empty(void)
{

    TEST_ASSERT_FALSE(BString__starts_with(&bstr(""), &bstr("9876")));
}
