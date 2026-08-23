#include <stdint.h>

#include "unity.h"

#include "../../../user/types/linked_list.h"

typedef uint64_t TestListType;

OptionalDef(TestListType);

static bool test_list_type_equals(void * a, void * b)
{
    return *(TestListType *) a == *(TestListType *) b;
}

LinkedListDefine(TestListType, TestList, &test_list_type_equals);
LinkedListBodyDefine(TestList);

void test_user_linked_list_heap_new_uses_user_allocator(void)
{
    TestList * list = TestList__heap_new();

    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_EQUAL(0, TestList__len(list));

    match_result(CollectionCode, TestList__push(list, 10), success(index) { TEST_ASSERT_EQUAL(0, index); }, error(code, message) { TEST_FAIL(); })

    TEST_ASSERT_EQUAL(1, TestList__len(list));
    TEST_ASSERT_EQUAL(10, Unwrap(TestListType, TestList__at(list, 0), (TestListType){}));

    match_result(CollectionCode, TestList__dispose(list), success(_) {}, error(code, message) { TEST_FAIL(); })

    free(list);
}
