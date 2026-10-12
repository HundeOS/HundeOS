#include <stdint.h>

#include "unity.h"

#include "shared_fixed_array.h"

typedef uint64_t TestType;
typedef TestType * TestTypePtr;

OptionalDef(TestType);

#define TEST_ARRAY_SIZE 32

static bool test_type_equals(void * a, void * b)
{
    return *(TestType *)a == *(TestType *)b;
}

FixedArrayDefine(TestType, TEST_ARRAY_SIZE, TestArray, &test_type_equals);
FixedArrayBodyDefine(TestArray);

FixedArrayDefine(TestType, 1, TinyTestArray, &test_type_equals);
FixedArrayBodyDefine(TinyTestArray);

FixedArrayDefine(TestType, 2, StackTestArray, &test_type_equals);
FixedArrayStackBodyDefine(StackTestArray);

static int compare_uint64(TestType * a, TestType * b)
{
    return *a - *b;
}

static bool greater_than(TestType * a, void * b)
{
    return *a > *(TestType *)b;
}

void test_fixed_array_push_and_at(void)
{
    TestArray array = TestArray__new();

    match_result(TestArray__push(&array, 10),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, TestArray__len(&array));
    TEST_ASSERT_EQUAL(10, unwrap_optional(TestArray__at(&array, 0), (TestType){}));
}

void test_stack_fixed_array_body_supports_existing_storage(void)
{
    StackTestArray array = StackTestArray__new();

    match_result(StackTestArray__push(&array, 42),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, StackTestArray__len(&array));
    TEST_ASSERT_EQUAL(42, unwrap_optional(StackTestArray__at(&array, 0), (TestType){}));
    uint64_t total = 0;
    for_each_fa_ptr(
        StackTestArray,
        &array,
        index,
        value,
        total += value;
    )
    ;
    TEST_ASSERT_EQUAL_UINT64(42, total);
}

void test_fixed_array_init_resets_existing_storage(void)
{
    TestArray array = TestArray__new();
    match_result(TestArray__push(&array, 42),
        success_do_nothing(),
        error_test_fail()
    );

    TestArray__init(&array);

    TEST_ASSERT_EQUAL(0, TestArray__len(&array));
    match_optional(TestArray__at(&array, 0),
        some_test_fail(),
        none_do_nothing()
    );
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
    match_result(
        TestArray__push_all(&array, &other),
        success(index)
        {
            TEST_ASSERT_EQUAL(1, index);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(11, TestArray__len(&array));
    TEST_ASSERT_EQUAL(9, unwrap_optional(TestArray__at(&array, -1), (TestType){}));
    match_optional(TestArray__at(&array, -12),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_fixed_array_find_remove_and_pop_front(void)
{
    TestArray array = TestArray__new();
    for (int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        TestArray__push(&array, i + 100);
    }

    match_optional(TestArray__find_item(&array, 107),
        some(value)
        {
            TEST_ASSERT_EQUAL(7, value);
        },
        none_test_fail()
    );

    match_result(TestArray__remove(&array, 107),
        success_do_nothing(),
        error_test_fail()
    );

    match_optional(TestArray__pop_front(&array),
        some(value)
        {
            TEST_ASSERT_EQUAL(100, value);
        },
        none_test_fail()
    );
}

void test_fixed_array_match_find_result_unwraps_found_value(void)
{
    TestArray array = TestArray__new();
    TestArray__push(&array, 100);
    TestArray__push(&array, 200);
    TestType threshold = 150;

    match_find_result(
        TestArray__find(&array, greater_than, &threshold),
        some(value)
        {
            TEST_ASSERT_EQUAL(200, value);
        },
        none_test_fail()
    );
}

void test_fixed_array_match_find_result_runs_none_branch(void)
{
    TestArray array = TestArray__new();
    TestArray__push(&array, 100);
    TestType threshold = 150;

    match_find_result(
        TestArray__find(&array, greater_than, &threshold),
        some_test_fail(),
        none()
        {
            TEST_ASSERT_TRUE(true);
        }
    );
}

void test_fixed_array_match_find_result_ptr_unwraps_found_pointer(void)
{
    TestArray array = TestArray__new();
    TestArray__push(&array, 100);
    TestArray__push(&array, 200);
    TestType threshold = 150;

    match_find_result_ptr(
        TestArray__find(&array, greater_than, &threshold),
        some(value)
        {
            *value = 300;
        },
        none_test_fail()
    );

    TEST_ASSERT_EQUAL(300, unwrap_optional(TestArray__at(&array, 1), (TestType){}));
}

void test_fixed_array_set_sort_and_filter(void)
{
    TestArray array = TestArray__new();

    match_result(TestArray__set(&array, TEST_ARRAY_SIZE - 1, 10),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(TEST_ARRAY_SIZE, TestArray__len(&array));

    TestArray unsorted = TestArray__new();
    for (int i = 10; i > 0; i--)
    {
        TestArray__push(&unsorted, i);
    }

    TestArray sorted = TestArray__sort_asc(&unsorted, compare_uint64);
    TEST_ASSERT_EQUAL(1, unwrap_optional(TestArray__at(&sorted, 0), (TestType){}));

    TestType five = 5;
    TestArray filtered = TestArray__filter(&unsorted, greater_than, &five);
    TEST_ASSERT_EQUAL(5, TestArray__len(&filtered));
}

void test_fixed_array_push_when_full_gives_error(void)
{
    TinyTestArray array = TinyTestArray__new();

    match_result(TinyTestArray__push(&array, 10),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(
        TinyTestArray__push(&array, 10),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(ARRAY_FULL, code);
        }
    );
}
