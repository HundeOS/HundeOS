#include <stdint.h>

#include "unity.h"

#include "../../../kernel/types/fixed_array.h"
#include "../../../kernel/types/comparison.h"

typedef uint64_t TestType;

OptionalDef(TestType);

FixedArrayDefine(TestType, 4, TestArray, SimpleEquals(uint64_t));
FixedArrayBodyDefine(TestArray);

void test_kernel_fixed_array_heap_new_uses_kernel_allocator(void)
{
    TestArray * array = TestArray__heap_new();

    TEST_ASSERT_NOT_NULL(array);
    TEST_ASSERT_EQUAL(0, TestArray__len(array));

    ResultMatch(CollectionCode, TestArray__push(array, 10))
        IsError(code, message, TEST_FAIL())
        IsSuccess(index, TEST_ASSERT_EQUAL(0, index))
    End

    TEST_ASSERT_EQUAL(1, TestArray__len(array));
    TEST_ASSERT_EQUAL(10, Unwrap(TestType, TestArray__at(array, 0), (TestType){}));
}
