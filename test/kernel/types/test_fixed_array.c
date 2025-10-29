#include <stdint.h>

#include "unity.h"

#include "../../../kernel/types/fixed_array.h"
#include "../../../kernel/types/result.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/types/comparison.h"
#include "../../../kernel/types/kstring.h"
#include "../../../kernel/types/kstring_optionals.h"

typedef uint64_t TestType;
typedef TestType * TestTypePtr;

OptionalDef(TestType);

#define TEST_ARRAY_SIZE 32

FixedArrayDefine(TestType, TEST_ARRAY_SIZE, TestArray, SimpleEquals(uint64_t));
FixedArrayBodyDefine(TestArray);

FixedArrayDefine(KString, TEST_ARRAY_SIZE, TestStringArray, kstring_equals);
FixedArrayBodyDefine(TestStringArray);

FixedArrayDefine(TestType, 1, TinyTestArray, SimpleEquals(uint64_t));
FixedArrayBodyDefine(TinyTestArray);


void test__fixed_array_push_and_at(void)
{
    TestType val = 10;
    TestArray array = TestArray__new();
    ResultMatch(ArrayCode, TestArray__push(&array, val));
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_EQUAL_UINT(1, array.len);

    Optional(TestType) opt = TestArray__at(&array, 0);
    Match(TestType, opt);
        IsNone(TEST_FAIL());
        IsSome(value, TEST_ASSERT_EQUAL_UINT(val, value));
    End
}

void test__fixed_array_at_ptr(void)
{
    TestType val = 10;
    TestArray array = TestArray__new();
    TestArray__push(&array, val);

    FAPtrMatch(TestArray, TestArray__ptr_at(&array, 0));
        IsNone(TEST_FAIL());
        IsSome(value, TEST_ASSERT_EQUAL_INT(val, *value));
    End
}

bool predicate_str(KStringPtr value, void * context)
{
    return KString__equals(value, (KStringPtr) context);
}

void test_fixed_array_find(void)
{
    TestType val = 10;
    TestStringArray array = TestStringArray__new();
    KString format = kstring("hello %i");
    for (int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        TestStringArray__push(&array, KString__sprintf(&format, i));
    }

    KString match = kstring("hello 10");

    FindResultMatch(TestStringArray, TestStringArray__find(&array, &predicate_str, &match));
        IsNone(TEST_FAIL());
        IsSome(found,
        {
            TEST_ASSERT_EQUAL(10, found.index);
            TEST_ASSERT_TRUE(KString__equals(&match, found.value));
        });
    End
}

void test_fixed_array_find_when_not_found(void)
{
    TestStringArray array = TestStringArray__new();
    KString format = kstring("hello %i");
    for (int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        TestStringArray__push(&array, KString__sprintf(&format, i));
    }

    KString match = kstring("not there");

    FindResultMatch(TestStringArray, TestStringArray__find(&array, &predicate_str, &match));
        IsSome(res, TEST_FAIL());
    End
}

void test_fixed_array_find_item_when_exists(void)
{
    TestArray array = TestArray__new();
    for (int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        TestArray__push(&array, i + 100);
    }

    Match(ArrayIndex, TestArray__find_item(&array, 107));
        IsNone(TEST_FAIL());
        IsSome(value, TEST_ASSERT_EQUAL_INT(7, value));
    End
}

void test_fixed_array_remove(void)
{
    TestArray array = TestArray__new();
    for (int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        TestArray__push(&array, i + 100);
    }

    ResultMatch(ArrayCode, TestArray__remove(&array, 107));
        IsError(code, message, TEST_FAIL());
        IsSuccess(code,
        {
            TEST_ASSERT_EQUAL_INT(TEST_ARRAY_SIZE - 1, array.len);
        });
    End

    Match(ArrayIndex, TestArray__find_item(&array, 27))
        IsSome(value, TEST_FAIL());
    End
}

void test__fixed_array_find_item_doesnt_exist(void)
{
    TestArray array = TestArray__new();
    for (int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        TestArray__push(&array, i + 100);
    }

    Match(ArrayIndex, TestArray__find_item(&array, 27));
        IsSome(value, TEST_FAIL());
    End
}

void test__fixed_array_iterate(void)
{
    TestArray array = TestArray__new();

    for (int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        TestArray__push(&array, i);
    }

    int count = 0;
    FAIterate(TestArray, array, i, v)
        TEST_ASSERT_EQUAL_INT(count, i);
        TEST_ASSERT_EQUAL_INT(count, v);
        count++;
    End
}

void test__fixed_array_push_when_full(void)
{
    TestType val = 10;
    TinyTestArray array = TinyTestArray__new();

    ResultMatch(ArrayCode, TinyTestArray__push(&array, val));
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(ArrayCode, TinyTestArray__push(&array, val));
        IsSuccess(value, TEST_FAIL());
        IsError(code, message, TEST_ASSERT_EQUAL_INT(ARRAY_FULL, code));
    End
}

void test__set_in_bounds(void)
{
    TestType val = 10;
    int index = TEST_ARRAY_SIZE - 1;
    TestArray array = TestArray__new();
    ResultMatch(ArrayCode, TestArray__set(&array, index,val));
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_EQUAL_INT(index + 1, array.len);

    Match(TestType, TestArray__at(&array, index));
        IsSome(value, TEST_ASSERT_EQUAL_INT(val, value))
    End
}

void test__set_out_of_bounds(void)
{
    TestType val = 10;
    TestArray array = TestArray__new();
    ResultMatch(ArrayCode, TestArray__set(&array, TEST_ARRAY_SIZE,val));
        IsError(code, message, TEST_ASSERT_EQUAL_INT(ARRAY_OUT_OF_BOUNDS, code));
        IsSuccess(_, TEST_FAIL());
    End
}
