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

    ResultMatch(DynStringCode, DynString__init(&text, 4))
        IsSuccess(code,
        {
            TEST_ASSERT_EQUAL_INT64(0, DynString__length(&text));
            TEST_ASSERT_EQUAL_INT64(4, DynString__capacity(&text));
            TEST_ASSERT_EQUAL_STRING("", DynString__cstr(&text));
            DynString__dispose(&text);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_dyn_string_grows_when_appending_past_capacity(void)
{
    DynString text;

    ResultMatch(DynStringCode, DynString__init(&text, 4))
        IsSuccess(code,
        {
            ResultMatch(DynStringCode, DynString__append_cstr(&text, "abcdef"))
                IsSuccess(append_code, {})
                IsError(append_code, message, TEST_FAIL())
            End

            TEST_ASSERT_EQUAL_INT64(6, DynString__length(&text));
            TEST_ASSERT_GREATER_OR_EQUAL_INT64(7, DynString__capacity(&text));
            TEST_ASSERT_EQUAL_STRING("abcdef", DynString__cstr(&text));
            DynString__dispose(&text);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_dyn_string_appends_bstring(void)
{
    DynString text;
    BString suffix = bstring(" world");

    ResultMatch(DynStringCode, DynString__init(&text, 8))
        IsSuccess(code,
        {
            ResultMatch(DynStringCode, DynString__append_cstr(&text, "hello"))
                IsSuccess(append_code, {})
                IsError(append_code, message, TEST_FAIL())
            End

            ResultMatch(DynStringCode, DynString__append_bstring(&text, &suffix))
                IsSuccess(append_code, {})
                IsError(append_code, message, TEST_FAIL())
            End

            TEST_ASSERT_EQUAL_STRING("hello world", DynString__cstr(&text));
            DynString__dispose(&text);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_dyn_string_clear_keeps_capacity(void)
{
    DynString text;

    ResultMatch(DynStringCode, DynString__init(&text, 4))
        IsSuccess(code,
        {
            ResultMatch(DynStringCode, DynString__append_cstr(&text, "abcdef"))
                IsSuccess(append_code, {})
                IsError(append_code, message, TEST_FAIL())
            End

            StringPos capacity = DynString__capacity(&text);
            DynString__clear(&text);
            TEST_ASSERT_EQUAL_INT64(0, DynString__length(&text));
            TEST_ASSERT_EQUAL_INT64(capacity, DynString__capacity(&text));
            TEST_ASSERT_EQUAL_STRING("", DynString__cstr(&text));
            DynString__dispose(&text);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_dyn_string_char_at_returns_character_by_index(void)
{
    DynString text;

    ResultMatch(DynStringCode, DynString__init(&text, 4))
        IsSuccess(code,
        {
            ResultMatch(DynStringCode, DynString__append_cstr(&text, "abc"))
                IsSuccess(append_code, {})
                IsError(append_code, message, TEST_FAIL())
            End

            Match(char, DynString__char_at(&text, 1))
                IsSome(ch, TEST_ASSERT_EQUAL_CHAR('b', ch))
                IsNone(TEST_FAIL())
            End

            Match(char, DynString__char_at(&text, 3))
                IsSome(ch, TEST_FAIL())
                IsNone({})
            End

            DynString__dispose(&text);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_dyn_string_bstring_converts_when_it_fits(void)
{
    DynString text;

    ResultMatch(DynStringCode, DynString__init(&text, 4))
        IsSuccess(code,
        {
            ResultMatch(DynStringCode, DynString__append_cstr(&text, "abc"))
                IsSuccess(append_code, {})
                IsError(append_code, message, TEST_FAIL())
            End

            Match(BString, DynString__bstring(&text))
                IsSome(value, TEST_ASSERT_EQUAL_STRING("abc", BString__cstr(&value)))
                IsNone(TEST_FAIL())
            End

            DynString__dispose(&text);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_dyn_string_lines_splits_into_owned_lines(void)
{
    DynString text;

    ResultMatch(DynStringCode, DynString__init(&text, 8))
        IsSuccess(code,
        {
            ResultMatch(DynStringCode, DynString__append_cstr(&text, "alpha\n\nbeta\n"))
                IsSuccess(append_code, {})
                IsError(append_code, message, TEST_FAIL())
            End

            Match(DynStringLines, DynString__lines(&text))
                IsSome(lines,
                {
                    TEST_ASSERT_EQUAL_UINT64(3, DynStringLines__len(&lines));

                    Match(DynString, DynStringLines__at(&lines, 0))
                        IsSome(line, TEST_ASSERT_EQUAL_STRING("alpha", DynString__cstr(&line)))
                        IsNone(TEST_FAIL())
                    End

                    Match(DynString, DynStringLines__at(&lines, 1))
                        IsSome(line, TEST_ASSERT_EQUAL_STRING("", DynString__cstr(&line)))
                        IsNone(TEST_FAIL())
                    End

                    Match(DynString, DynStringLines__at(&lines, 2))
                        IsSome(line, TEST_ASSERT_EQUAL_STRING("beta", DynString__cstr(&line)))
                        IsNone(TEST_FAIL())
                    End

                    DynStringLines__dispose_all(&lines);
                })
                IsNone(TEST_FAIL())
            End

            DynString__dispose(&text);
        })
        IsError(code, message, TEST_FAIL())
    End
}
