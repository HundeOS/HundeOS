#include "unity.h"
#include "../../../common/types/bstring.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_bstring_clear_resets_string(void)
{
    BString str = bstring("hello");

    BString__clear(&str);

    TEST_ASSERT_TRUE(BString__is_empty(&str));
    TEST_ASSERT_EQUAL_STRING("", BString__cstr(&str));
}

void test_bstring_push_char_appends_until_capacity_limit(void)
{
    BString str = bstring("ab");

    TEST_ASSERT_TRUE(BString__push_char(&str, 'c'));
    TEST_ASSERT_EQUAL_INT64(3, str.len);
    TEST_ASSERT_EQUAL_STRING("abc", BString__cstr(&str));
}

void test_bstring_push_char_rejects_when_full(void)
{
    BString str = EMPTY_BSTRING;

    for (StringPos i = 0; i < BSTRING_MAX_SIZE - 1; i++)
    {
        TEST_ASSERT_TRUE(BString__push_char(&str, 'x'));
    }

    TEST_ASSERT_FALSE(BString__push_char(&str, 'y'));
    TEST_ASSERT_EQUAL_INT64(BSTRING_MAX_SIZE - 1, str.len);
}

void test_bstring_insert_char_inserts_at_requested_position(void)
{
    BString str = bstring("ac");

    TEST_ASSERT_TRUE(BString__insert_char(&str, 1, 'b'));
    TEST_ASSERT_EQUAL_INT64(3, str.len);
    TEST_ASSERT_EQUAL_STRING("abc", BString__cstr(&str));
}

void test_bstring_insert_char_rejects_out_of_bounds_index(void)
{
    BString str = bstring("ab");

    TEST_ASSERT_FALSE(BString__insert_char(&str, 3, 'c'));
    TEST_ASSERT_EQUAL_STRING("ab", BString__cstr(&str));
}

void test_bstring_remove_at_removes_requested_position(void)
{
    BString str = bstring("abc");

    TEST_ASSERT_TRUE(BString__remove_at(&str, 1));
    TEST_ASSERT_EQUAL_INT64(2, str.len);
    TEST_ASSERT_EQUAL_STRING("ac", BString__cstr(&str));
}

void test_bstring_remove_at_rejects_out_of_bounds_index(void)
{
    BString str = bstring("ab");

    TEST_ASSERT_FALSE(BString__remove_at(&str, 2));
    TEST_ASSERT_EQUAL_STRING("ab", BString__cstr(&str));
}

void test_bstring_is_whitespace_matches_shell_whitespace_set(void)
{
    TEST_ASSERT_TRUE(BString__is_whitespace(' '));
    TEST_ASSERT_TRUE(BString__is_whitespace('\t'));
    TEST_ASSERT_TRUE(BString__is_whitespace('\n'));
    TEST_ASSERT_TRUE(BString__is_whitespace('\r'));
    TEST_ASSERT_TRUE(BString__is_whitespace('\0'));
    TEST_ASSERT_FALSE(BString__is_whitespace('x'));
}

void test_bstring_strip_removes_leading_and_trailing_whitespace(void)
{
    BString input = bstring(" \t hello world \n");

    BString stripped = BString__strip(&input);

    TEST_ASSERT_EQUAL_STRING("hello world", BString__cstr(&stripped));
}

void test_bstring_strip_returns_empty_for_whitespace_only_input(void)
{
    BString input = bstring(" \t\r\n");

    BString stripped = BString__strip(&input);

    TEST_ASSERT_TRUE(BString__is_empty(&stripped));
}

void test_bstring_slice_returns_requested_range(void)
{
    BString input = bstring("abcdef");

    BString slice = BString__slice(&input, 1, 4);

    TEST_ASSERT_EQUAL_STRING("bcd", BString__cstr(&slice));
}

void test_bstring_slice_uses_minus_one_as_end_of_string(void)
{
    BString input = bstring("abcdef");

    BString slice = BString__slice(&input, 2, -1);

    TEST_ASSERT_EQUAL_STRING("cdef", BString__cstr(&slice));
}

void test_bstring_slice_clamps_out_of_range_positions(void)
{
    BString input = bstring("abcdef");

    BString slice = BString__slice(&input, -4, 99);

    TEST_ASSERT_EQUAL_STRING("abcdef", BString__cstr(&slice));
}

void test_bstring_slice_returns_empty_when_start_is_not_before_end(void)
{
    BString input = bstring("abcdef");

    BString slice = BString__slice(&input, 4, 1);

    TEST_ASSERT_TRUE(BString__is_empty(&slice));
}

void test_bstring_split_first_any_returns_none_for_empty_input(void)
{
    BString empty = EMPTY_BSTRING;
    BString delimiters = bstring(" \t");

    BStringSplit split = BString__split_first_any(&empty, &delimiters);

    TEST_ASSERT_FALSE(split.match.present);
    TEST_ASSERT_TRUE(BString__is_empty(&split.rest));
}

void test_bstring_split_first_any_splits_at_first_matching_delimiter(void)
{
    BString input = bstring("alpha beta");
    BString delimiters = bstring(" \t");

    BStringSplit split = BString__split_first_any(&input, &delimiters);

    TEST_ASSERT_TRUE(split.match.present);
    TEST_ASSERT_EQUAL_STRING("alpha", BString__cstr(&split.match.some.val));
    TEST_ASSERT_EQUAL_STRING("beta", BString__cstr(&split.rest));
}

void test_bstring_split_first_any_returns_empty_match_when_delimiter_is_first(void)
{
    BString input = bstring("\"tail");
    BString delimiters = bstring("\"");

    BStringSplit split = BString__split_first_any(&input, &delimiters);

    TEST_ASSERT_TRUE(split.match.present);
    TEST_ASSERT_TRUE(BString__is_empty(&split.match.some.val));
    TEST_ASSERT_EQUAL_STRING("tail", BString__cstr(&split.rest));
}

void test_bstring_split_first_any_returns_whole_input_when_no_delimiter_found(void)
{
    BString input = bstring("token");
    BString delimiters = bstring(" \t");

    BStringSplit split = BString__split_first_any(&input, &delimiters);

    TEST_ASSERT_TRUE(split.match.present);
    TEST_ASSERT_EQUAL_STRING("token", BString__cstr(&split.match.some.val));
    TEST_ASSERT_TRUE(BString__is_empty(&split.rest));
}
