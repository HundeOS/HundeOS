#include <stdint.h>

#include "unity.h"

#include "../../../kernel/types/linked_list.h"

typedef uint64_t TestListType;
typedef TestListType * TestListTypePtr;

OptionalDef(TestListType);

LinkedListDefine(TestListType, TestList, SimpleEquals(uint64_t));
LinkedListBodyDefine(TestList);

void test_kernel_linked_list_heap_new_uses_kernel_allocator(void)
{
    TestList * list = TestList__heap_new();

    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_EQUAL(0, TestList__len(list));
    ResultMatch(CollectionCode, TestList__push(list, 10))
        IsError(code, message, TEST_FAIL())
        IsSuccess(index, TEST_ASSERT_EQUAL(0, index))
    End

    TEST_ASSERT_EQUAL(1, TestList__len(list));
    TEST_ASSERT_EQUAL(10, Unwrap(TestListType, TestList__at(list, 0), (TestListType){}));
    ResultMatch(CollectionCode, TestList__dispose(list))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(0, TestList__len(list));
    TEST_ASSERT_NULL(list->head);
    TEST_ASSERT_NULL(list->tail);
}
