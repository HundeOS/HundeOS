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
            TEST_ASSERT_TRUE(DynString__append_cstr(&text, "abcdef").ok);
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
            TEST_ASSERT_TRUE(DynString__append_cstr(&text, "hello").ok);
            TEST_ASSERT_TRUE(DynString__append_bstring(&text, &suffix).ok);
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
            TEST_ASSERT_TRUE(DynString__append_cstr(&text, "abcdef").ok);
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
