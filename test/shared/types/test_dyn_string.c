#include "unity.h"
#include "../../../common/types/dyn_string.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_dyn_string_initialises_empty_with_capacity(void)
{
    DynString text;

    match_result(DynStringCode, DynString__init(&text, 4), success(code) {
            TEST_ASSERT_EQUAL_INT64(0, DynString__length(&text));
            TEST_ASSERT_EQUAL_INT64(4, DynString__capacity(&text));
            TEST_ASSERT_EQUAL_STRING("", DynString__cstr(&text));
            DynString__dispose(&text);
        }, error_test_fail());
}

void test_dyn_string_grows_when_appending_past_capacity(void)
{
    DynString text;

    match_result(DynStringCode, DynString__init(&text, 4), success(code) {
            match_result(DynStringCode, DynString__append_cstr(&text, "abcdef"), success_do_nothing(), error_test_fail());

            TEST_ASSERT_EQUAL_INT64(6, DynString__length(&text));
            TEST_ASSERT_GREATER_OR_EQUAL_INT64(7, DynString__capacity(&text));
            TEST_ASSERT_EQUAL_STRING("abcdef", DynString__cstr(&text));
            DynString__dispose(&text);
        }, error_test_fail());
}

void test_dyn_string_appends_bstring(void)
{
    DynString text;
    BString suffix = bstring(" world");

    match_result(DynStringCode, DynString__init(&text, 8), success(code) {
            match_result(DynStringCode, DynString__append_cstr(&text, "hello"), success_do_nothing(), error_test_fail());

            match_result(DynStringCode, DynString__append_bstring(&text, &suffix), success_do_nothing(), error_test_fail());

            TEST_ASSERT_EQUAL_STRING("hello world", DynString__cstr(&text));
            DynString__dispose(&text);
        }, error_test_fail());
}

void test_dyn_string_clear_keeps_capacity(void)
{
    DynString text;

    match_result(DynStringCode, DynString__init(&text, 4), success(code) {
            match_result(DynStringCode, DynString__append_cstr(&text, "abcdef"), success_do_nothing(), error_test_fail());

            StringPos capacity = DynString__capacity(&text);
            DynString__clear(&text);
            TEST_ASSERT_EQUAL_INT64(0, DynString__length(&text));
            TEST_ASSERT_EQUAL_INT64(capacity, DynString__capacity(&text));
            TEST_ASSERT_EQUAL_STRING("", DynString__cstr(&text));
            DynString__dispose(&text);
        }, error_test_fail());
}

void test_dyn_string_char_at_returns_character_by_index(void)
{
    DynString text;

    match_result(DynStringCode, DynString__init(&text, 4), success(code) {
            match_result(DynStringCode, DynString__append_cstr(&text, "abc"), success_do_nothing(), error_test_fail());

            match_optional(char, DynString__char_at(&text, 1), some(ch) { TEST_ASSERT_EQUAL_CHAR('b', ch); }, none_test_fail());

            match_optional(char, DynString__char_at(&text, 3), some_test_fail(), none_do_nothing());

            DynString__dispose(&text);
        }, error_test_fail());
}

void test_dyn_string_bstring_converts_when_it_fits(void)
{
    DynString text;

    match_result(DynStringCode, DynString__init(&text, 4), success(code) {
            match_result(DynStringCode, DynString__append_cstr(&text, "abc"), success_do_nothing(), error_test_fail());

            match_optional(BString, DynString__bstring(&text), some(value) { TEST_ASSERT_EQUAL_STRING("abc", BString__cstr(&value)); }, none_test_fail());

            DynString__dispose(&text);
        }, error_test_fail());
}

void test_dyn_string_lines_splits_into_owned_lines(void)
{
    DynString text;

    match_result(DynStringCode, DynString__init(&text, 8), success(code) {
            match_result(DynStringCode, DynString__append_cstr(&text, "alpha\n\nbeta\n"), success_do_nothing(), error_test_fail());

            match_optional(DynStringLines, DynString__lines(&text), some(lines) {
                    TEST_ASSERT_EQUAL_UINT64(3, DynStringLines__len(&lines));

                    match_optional(DynString, DynStringLines__at(&lines, 0), some(line) { TEST_ASSERT_EQUAL_STRING("alpha", DynString__cstr(&line)); }, none_test_fail());

                    match_optional(DynString, DynStringLines__at(&lines, 1), some(line) { TEST_ASSERT_EQUAL_STRING("", DynString__cstr(&line)); }, none_test_fail());

                    match_optional(DynString, DynStringLines__at(&lines, 2), some(line) { TEST_ASSERT_EQUAL_STRING("beta", DynString__cstr(&line)); }, none_test_fail());

                    DynStringLines__dispose_all(&lines);
                }, none_test_fail());

            DynString__dispose(&text);
        }, error_test_fail());
}
