#include <stdint.h>

#include "unity.h"

#include "shared_fixed_array.h"

typedef uint64_t TestType;
typedef TestType * TestTypePtr;

OptionalDef(TestType);

#define TEST_ARRAY_SIZE 32

static bool test_type_equals(void * a, void * b)
{
    return *(TestType *) a == *(TestType *) b;
}

FixedArrayDefine(TestType, TEST_ARRAY_SIZE, TestArray, &test_type_equals);
FixedArrayBodyDefine(TestArray);

FixedArrayDefine(TestType, 1, TinyTestArray, &test_type_equals);
FixedArrayBodyDefine(TinyTestArray);

static int compare_uint64(TestType * a, TestType * b)
{
    return *a - *b;
}

static bool greater_than(TestType * a, void * b)
{
    return *a > *(TestType *) b;
}

void test_fixed_array_push_and_at(void)
{
    TestArray array = TestArray__new();

    ResultMatch(CollectionCode, TestArray__push(&array, 10))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(1, TestArray__len(&array));
    TEST_ASSERT_EQUAL(10, Unwrap(TestType, TestArray__at(&array, 0), (TestType){}));
}

void test_fixed_array_push_all_and_negative_index(void)
{
    TestArray array = TestArray__new();
    TestArray other = TestArray__new();

    for (int i = 0; i < 10; i++)
    {
        TestArray__push(&other, i);
    }

    TestArray__push(&array, 123);
    ResultMatch(CollectionCode, TestArray__push_all(&array, &other))
        IsError(code, message, TEST_FAIL())
        IsSuccess(index, TEST_ASSERT_EQUAL(1, index))
    End

    TEST_ASSERT_EQUAL(11, TestArray__len(&array));
    TEST_ASSERT_EQUAL(9, Unwrap(TestType, TestArray__at(&array, -1), (TestType){}));
}

void test_fixed_array_find_remove_and_pop_front(void)
{
    TestArray array = TestArray__new();
    for (int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        TestArray__push(&array, i + 100);
    }

    Match(ArrayIndex, TestArray__find_item(&array, 107))
        IsNone(TEST_FAIL())
        IsSome(value, TEST_ASSERT_EQUAL(7, value))
    End

    ResultMatch(CollectionCode, TestArray__remove(&array, 107))
        IsError(code, message, TEST_FAIL())
    End

    Match(TestType, TestArray__pop_front(&array))
        IsNone(TEST_FAIL())
        IsSome(value, TEST_ASSERT_EQUAL(100, value))
    End
}

void test_fixed_array_set_sort_and_filter(void)
{
    TestArray array = TestArray__new();

    ResultMatch(CollectionCode, TestArray__set(&array, TEST_ARRAY_SIZE - 1, 10))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(TEST_ARRAY_SIZE, TestArray__len(&array));

    TestArray unsorted = TestArray__new();
    for (int i = 10; i > 0; i--)
    {
        TestArray__push(&unsorted, i);
    }

    TestArray sorted = TestArray__sort_asc(&unsorted, compare_uint64);
    TEST_ASSERT_EQUAL(1, Unwrap(TestType, TestArray__at(&sorted, 0), (TestType){}));

    TestType five = 5;
    TestArray filtered = TestArray__filter(&unsorted, greater_than, &five);
    TEST_ASSERT_EQUAL(5, TestArray__len(&filtered));
}

void test_fixed_array_push_when_full_gives_error(void)
{
    TinyTestArray array = TinyTestArray__new();

    ResultMatch(CollectionCode, TinyTestArray__push(&array, 10))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(CollectionCode, TinyTestArray__push(&array, 10))
        IsSuccess(value, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(ARRAY_FULL, code))
    End
}
