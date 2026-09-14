#include "unity.h"
#include "../../../common/types/bstring.h"

void test_bstring_assertion_compares_embedded_nuls(void)
{
    BString expected = bstr("a\0b");
    BString actual = BString__from_chars("a\0b", 3);
    TEST_ASSERT_EQUAL_BSTRING(expected, actual);
}

void test_bstring_assertion_evaluates_arguments_once(void)
{
    BString value = bstr("value");
    unsigned expected_calls = 0;
    unsigned actual_calls = 0;
    TEST_ASSERT_EQUAL_BSTRING((expected_calls++, value), (actual_calls++, value));
    TEST_ASSERT_EQUAL_UINT(1, expected_calls);
    TEST_ASSERT_EQUAL_UINT(1, actual_calls);
}

void test_bstring_assertion_accepts_returned_values_and_dereferenced_pointers(void)
{
    BString value = bstr("VALUE");
    ConstBStringPtr pointer = &value;
    TEST_ASSERT_EQUAL_BSTRING(BString__uppercase(&bstr("value")), *pointer);
}

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
    TEST_ASSERT_EQUAL_BSTRING(bstr(""), str);
}

void test_bstring_push_char_appends_until_capacity_limit(void)
{
    BString str = bstring("ab");

    TEST_ASSERT_TRUE(BString__push_char(&str, 'c'));
    TEST_ASSERT_EQUAL_INT64(3, str.len);
    TEST_ASSERT_EQUAL_BSTRING(bstr("abc"), str);
}

void test_bstring_push_char_rejects_when_full(void)
{
    BString str = empty_bstr();

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
    TEST_ASSERT_EQUAL_BSTRING(bstr("abc"), str);
}

void test_bstring_insert_char_rejects_out_of_bounds_index(void)
{
    BString str = bstring("ab");

    TEST_ASSERT_FALSE(BString__insert_char(&str, 3, 'c'));
    TEST_ASSERT_EQUAL_BSTRING(bstr("ab"), str);
}

void test_bstring_set_char_at_updates_existing_character(void)
{
    BString str = bstring("abc");

    TEST_ASSERT_TRUE(BString__set_char_at(&str, 1, 'x'));

    TEST_ASSERT_EQUAL_INT64(3, BString__length(&str));
    TEST_ASSERT_EQUAL_BSTRING(bstr("axc"), str);
}

void test_bstring_set_char_at_extends_length(void)
{
    BString str = bstring("ab");

    TEST_ASSERT_TRUE(BString__set_char_at(&str, 2, 'c'));

    TEST_ASSERT_EQUAL_INT64(3, BString__length(&str));
    TEST_ASSERT_EQUAL_BSTRING(bstr("abc"), str);
}

void test_bstring_set_char_at_rejects_out_of_bounds_index(void)
{
    BString str = bstring("ab");

    TEST_ASSERT_FALSE(BString__set_char_at(&str, BSTRING_MAX_SIZE - 1, 'x'));
    TEST_ASSERT_EQUAL_INT64(2, BString__length(&str));
    TEST_ASSERT_EQUAL_BSTRING(bstr("ab"), str);
}

void test_bstring_remove_at_removes_requested_position(void)
{
    BString str = bstring("abc");

    TEST_ASSERT_TRUE(BString__remove_at(&str, 1));
    TEST_ASSERT_EQUAL_INT64(2, str.len);
    TEST_ASSERT_EQUAL_BSTRING(bstr("ac"), str);
}

void test_bstring_remove_at_rejects_out_of_bounds_index(void)
{

    BString str = bstring("ab");

    TEST_ASSERT_FALSE(BString__remove_at(&str, 2));
    TEST_ASSERT_EQUAL_BSTRING(bstr("ab"), str);
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

    BString stripped = BString__strip(&bstr(" \t hello world \n"));

    TEST_ASSERT_EQUAL_BSTRING(bstr("hello world"), stripped);
}

void test_bstring_strip_returns_empty_for_whitespace_only_input(void)
{
    BString input = bstring(" \t\r\n");

    BString stripped = BString__strip(&bstr(" \t\r\n"));

    TEST_ASSERT_TRUE(BString__is_empty(&stripped));
}

void test_bstring_slice_returns_requested_range(void)
{
    BString input = bstring("abcdef");

    BString slice = BString__slice(&bstr("abcdef"), 1, 4);

    TEST_ASSERT_EQUAL_BSTRING(bstr("bcd"), slice);
}

void test_bstring_slice_uses_minus_one_as_end_of_string(void)
{
    BString input = bstring("abcdef");

    BString slice = BString__slice(&bstr("abcdef"), 2, -1);

    TEST_ASSERT_EQUAL_BSTRING(bstr("cdef"), slice);
}

void test_bstring_slice_clamps_out_of_range_positions(void)
{
    BString input = bstring("abcdef");

    BString slice = BString__slice(&bstr("abcdef"), -4, 99);

    TEST_ASSERT_EQUAL_BSTRING(bstr("abcdef"), slice);
}

void test_bstring_slice_returns_empty_when_start_is_not_before_end(void)
{
    BString input = bstring("abcdef");

    BString slice = BString__slice(&bstr("abcdef"), 4, 1);

    TEST_ASSERT_TRUE(BString__is_empty(&slice));
}

void test_bstring_split_first_any_returns_none_for_empty_input(void)
{
    BString empty = empty_bstr();
    BString delimiters = bstring(" \t");

    BStringSplit split = BString__split_first_any(&empty, &bstr(" \t"));

    TEST_ASSERT_FALSE(split.match.present);
    TEST_ASSERT_TRUE(BString__is_empty(&split.rest));
}

void test_bstring_split_first_any_splits_at_first_matching_delimiter(void)
{
    BString input = bstring("alpha beta");
    BString delimiters = bstring(" \t");

    BStringSplit split = BString__split_first_any(&bstr("alpha beta"), &bstr(" \t"));

    match_optional(split.match,
        some(value) { TEST_ASSERT_EQUAL_BSTRING(bstr("alpha"), value); },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL_BSTRING(bstr("beta"), split.rest);
}

void test_bstring_split_first_any_returns_empty_match_when_delimiter_is_first(void)
{
    BString input = bstring("\"tail");
    BString delimiters = bstring("\"");

    BStringSplit split = BString__split_first_any(&bstr("\"tail"), &bstr("\""));

    match_optional(split.match,
        some(value) { TEST_ASSERT_EQUAL_BSTRING(empty_bstr(), value); },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL_BSTRING(bstr("tail"), split.rest);
}

void test_bstring_split_first_any_returns_whole_input_when_no_delimiter_found(void)
{
    BString input = bstring("token");
    BString delimiters = bstring(" \t");

    BStringSplit split = BString__split_first_any(&bstr("token"), &bstr(" \t"));

    match_optional(split.match,
        some(value) { TEST_ASSERT_EQUAL_BSTRING(bstr("token"), value); },
        none_test_fail()
    );
    TEST_ASSERT_TRUE(BString__is_empty(&split.rest));
}

void test_bstring_split_first_token_strips_before_splitting(void)
{
    BString input = bstring("  alpha   beta");

    BStringSplit split = BString__split_first_token(&bstr("  alpha   beta"), &bstr(" \t"));

    match_optional(
        split.match,
        some(token)
        {
            TEST_ASSERT_EQUAL_BSTRING(bstr("alpha"), token);
            TEST_ASSERT_EQUAL_BSTRING(bstr("  beta"), split.rest);
        },
        none_test_fail()
    );
}

void test_bstring_parse_uint64_returns_value_for_valid_decimal(void)
{
    BString input = bstring("12345");

    match_optional(
        BString__parse_uint64(&bstr("12345")),
        some(value) { TEST_ASSERT_EQUAL_UINT64(12345, value); },
        none_test_fail()
    );
}

void test_bstring_parse_uint64_base_zero_accepts_hex_prefix(void)
{
    BString input = bstring("0xfe340000");

    match_optional(
        BString__parse_uint64_base(&bstr("0xfe340000"), 0),
        some(value) { TEST_ASSERT_EQUAL_HEX64(0xfe340000, value); },
        none_test_fail()
    );
}

void test_bstring_parse_uint64_base_strips_whitespace(void)
{
    BString input = bstring(" 126\n");

    match_optional(
        BString__parse_uint64_base(&bstr(" 126\n"), 0),
        some(value) { TEST_ASSERT_EQUAL_UINT64(126, value); },
        none_test_fail()
    );
}

void test_bstring_parse_uint64_rejects_empty_input(void)
{
    BString input = empty_bstr();

    TEST_ASSERT_FALSE(BString__parse_uint64(&bstr("")).present);
}

void test_bstring_parse_uint64_rejects_non_digit_input(void)
{
    BString input = bstring("12x");

    TEST_ASSERT_FALSE(BString__parse_uint64(&bstr("12x")).present);
}

void test_bstring_parse_uint64_base_rejects_trailing_text(void)
{
    BString input = bstring("0xfe340000tail");

    match_optional(BString__parse_uint64_base(&bstr("0xfe340000tail"), 0),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_bstring_uppercase_converts_ascii_lowercase(void)
{
    BString input = bstring("Sigterm-42");

    BString uppercase = BString__uppercase(&bstr("Sigterm-42"));

    TEST_ASSERT_EQUAL_BSTRING(bstr("SIGTERM-42"), uppercase);
}

void test_bstring_length_returns_stored_length(void)
{
    BString input = bstring("hello");

    TEST_ASSERT_EQUAL_INT64(5, BString__length(&bstr("hello")));
}

void test_bstring_char_at_returns_character_when_in_bounds(void)
{
    BString input = bstring("hello");

    match_optional(
        BString__char_at(&bstr("hello"), 1),
        some(ch) { TEST_ASSERT_EQUAL_CHAR('e', ch); },
        none_test_fail()
    );
}

void test_bstring_char_at_returns_none_when_out_of_bounds(void)
{
    BString input = bstring("hello");

    TEST_ASSERT_FALSE(BString__char_at(&bstr("hello"), 5).present);
}

void test_bstring_contains_char_reports_presence(void)
{
    BString input = bstring("hunde");

    TEST_ASSERT_TRUE(BString__contains_char(&bstr("hunde"), 'u'));
    TEST_ASSERT_FALSE(BString__contains_char(&bstr("hunde"), 'x'));
}

void test_bstring_from_chars_copies_requested_length(void)
{
    char input[] = {'h', 'u', 'n', 'd', 'e', 'x'};

    BString result = BString__from_chars(input, 5);

    TEST_ASSERT_EQUAL_INT64(5, BString__length(&result));
    TEST_ASSERT_EQUAL_BSTRING(bstr("hunde"), result);
}

void test_bstring_iterate_visits_each_character(void)
{
    BString output = empty_bstr();

    for_each_bstring(&bstr("abc"), index, ch, { BString__push_char(&output, ch); });

    TEST_ASSERT_EQUAL_BSTRING(bstr("abc"), output);
}

void test_bstring_sprintf_formats_unsigned_decimal(void)
{
    BString format = bstring("value=%u");

    BString formatted = BString__sprintf(&bstr("value=%u"), 4000000000U);

    TEST_ASSERT_EQUAL_BSTRING(bstr("value=4000000000"), formatted);
}

void test_bstring_sprintf_formats_signed_64_bit_decimal(void)
{
    BString format = bstring("value=%lld");

    BString formatted = BString__sprintf(&bstr("value=%lld"), -4294967296LL);

    TEST_ASSERT_EQUAL_BSTRING(bstr("value=-4294967296"), formatted);
}

void test_bstring_sprintf_formats_unsigned_64_bit_decimal(void)
{
    BString format = bstring("value=%llu");

    BString formatted = BString__sprintf(&bstr("value=%llu"), 4294967296ULL);

    TEST_ASSERT_EQUAL_BSTRING(bstr("value=4294967296"), formatted);
}

void test_bstring_sprintf_formats_unsigned_64_bit_hex(void)
{
    BString format = bstring("addr=0x%llx");

    BString formatted = BString__sprintf(&bstr("addr=0x%llx"), 0xffff000000600000ULL);

    TEST_ASSERT_EQUAL_BSTRING(bstr("addr=0xffff000000600000"), formatted);
}

void test_bstring_sprintf_formats_pointer_width_hex(void)
{
    BString format = bstring("addr=%p");

    BString formatted = BString__sprintf(&bstr("addr=%p"), (uintptr_t)0xffff000000600000ULL);

    TEST_ASSERT_EQUAL_BSTRING(bstr("addr=0xffff000000600000"), formatted);
}

void test_bstring_sprintf_formats_bstring(void)
{
    BString format = bstring("path=%B");
    BString path = bstring("/dev/virtio1");

    BString formatted = BString__sprintf(&bstr("path=%B"), &path);

    TEST_ASSERT_EQUAL_BSTRING(bstr("path=/dev/virtio1"), formatted);
}

void test_bstring_sprintf_formats_bstring_by_length(void)
{
    BString format = bstring("value=%B!");
    BString value = BString__from_chars("abcXYZ", 3);

    BString formatted = BString__sprintf(&bstr("value=%B!"), &value);

    TEST_ASSERT_EQUAL_BSTRING(bstr("value=abc!"), formatted);
}

void test_bstring_sprintf_formats_left_aligned_width(void)
{
    BString format = bstring("'%-*s'");

    BString formatted = BString__sprintf(&bstr("'%-*s'"), 5, "id");

    TEST_ASSERT_EQUAL_BSTRING(bstr("'id   '"), formatted);
}

void test_bstring_sprintf_formats_star_precision_for_string(void)
{

    BString formatted = BString__sprintf(&bstr("%-*.*s!"), 6, 3, "abcdef");

    TEST_ASSERT_EQUAL_BSTRING(bstr("abc   !"), formatted);
}
