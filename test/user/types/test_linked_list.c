#include <stdint.h>

#include "unity.h"

#include "../../../user/lib/types/linked_list.h"

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

    match_result(CollectionCode, TestList__push(list, 10), success(index) { TEST_ASSERT_EQUAL(0, index); }, error_test_fail());

    TEST_ASSERT_EQUAL(1, TestList__len(list));
    TEST_ASSERT_EQUAL(10, unwrap(TestListType, TestList__at(list, 0), (TestListType){}));

    match_result(CollectionCode, TestList__dispose(list), success_do_nothing(), error_test_fail());

    free(list);
}
