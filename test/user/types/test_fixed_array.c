#include <stdint.h>

#include "unity.h"

#include "../../../user/lib/types/fixed_array.h"

typedef uint64_t TestType;

OptionalDef(TestType);

static bool test_type_equals(void * a, void * b)
{
    return *(TestType *)a == *(TestType *)b;
}

FixedArrayDefine(TestType, 4, TestArray, &test_type_equals);
FixedArrayBodyDefine(TestArray);

void test_user_fixed_array_heap_new_uses_user_allocator(void)
{
    TestArray * array = TestArray__heap_new();

    TEST_ASSERT_NOT_NULL(array);
    TEST_ASSERT_EQUAL(0, TestArray__len(array));

    match_result(TestArray__push(array, 10),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(10, unwrap_optional(TestArray__at(array, 0), (TestType){}));
    free(array);
}
