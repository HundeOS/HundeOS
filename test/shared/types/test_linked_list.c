#include <stdint.h>
#include "unity.h"

#include "shared_linked_list.h"

typedef uint64_t TestListType;
typedef TestListType * TestListTypePtr;

OptionalDef(TestListType);

static bool test_list_type_equals(void * a, void * b)
{
    return *(TestListType *) a == *(TestListType *) b;
}

LinkedListDefine(TestListType, TestList, &test_list_type_equals);
LinkedListBodyDefine(TestList);

static bool value_matches(TestListTypePtr value, void * context)
{
    TestListType expected = *(TestListTypePtr) context;
    return *value == expected;
}

void test_linked_list_new_macro_initializes_empty_list(void)
{
    TestList list = LINKED_LIST_NEW();

    TEST_ASSERT_EQUAL(0, TestList__len(&list));
    TEST_ASSERT_NULL(list.head);
    TEST_ASSERT_NULL(list.tail);
}

void test_linked_list_push_and_at(void)
{
    TestList list = TestList__new();

    match_result(CollectionCode, TestList__push(&list, 10), success(index) { TEST_ASSERT_EQUAL(0, index); }, error_test_fail());

    TEST_ASSERT_EQUAL(1, TestList__len(&list));
    TEST_ASSERT_EQUAL(10, Unwrap(TestListType, TestList__at(&list, 0), (TestListType){}));
}

void test_linked_list_at_with_negative_index_gives_from_end(void)
{
    TestList list = TestList__new();
    TestList__push(&list, 10);
    TestList__push(&list, 20);
    TestList__push(&list, 30);

    TEST_ASSERT_EQUAL(30, Unwrap(TestListType, TestList__at(&list, -1), (TestListType){}));
    TEST_ASSERT_EQUAL(20, Unwrap(TestListType, TestList__at(&list, -2), (TestListType){}));
}

void test_linked_list_find(void)
{
    TestList list = TestList__new();
    TestList__push(&list, 10);
    TestList__push(&list, 20);
    TestList__push(&list, 30);

    TestListType expected = 20;

    match_optional(TestList__FindResult, TestList__find(&list, value_matches, &expected), some(found) {
            TEST_ASSERT_EQUAL(1, found.index);
            TEST_ASSERT_EQUAL(20, *found.value);
        }, none_test_fail());
}

void test_linked_list_iterate_visits_items_in_order(void)
{
    TestList list = TestList__new();
    TestList__push(&list, 10);
    TestList__push(&list, 20);
    TestList__push(&list, 30);

    TestListType expected = 10;
    uint64_t count = 0;

    for_each_ll(TestList, list, value,
    {
        TEST_ASSERT_EQUAL(expected, value);
        expected += 10;
        count++;
    });

    TEST_ASSERT_EQUAL(3, count);
}

void test_linked_list_remove_at_unlinks_middle_node(void)
{
    TestList list = TestList__new();
    TestList__push(&list, 10);
    TestList__push(&list, 20);
    TestList__push(&list, 30);

    Optional(TestListType) removed = TestList__remove_at(&list, 1);
    TEST_ASSERT_EQUAL(20, Unwrap(TestListType, removed, (TestListType){}));

    TEST_ASSERT_EQUAL(2, TestList__len(&list));
    TEST_ASSERT_EQUAL(10, Unwrap(TestListType, TestList__at(&list, 0), (TestListType){}));
    TEST_ASSERT_EQUAL(30, Unwrap(TestListType, TestList__at(&list, 1), (TestListType){}));
}

void test_linked_list_remove_uses_equals_function(void)
{
    TestList list = TestList__new();
    TestList__push(&list, 10);
    TestList__push(&list, 20);
    TestList__push(&list, 30);

    match_result(CollectionCode, TestList__remove(&list, 20), success_do_nothing(), error_test_fail());

    TEST_ASSERT_EQUAL(2, TestList__len(&list));
    TEST_ASSERT_EQUAL(30, Unwrap(TestListType, TestList__at(&list, 1), (TestListType){}));
}

void test_linked_list_dispose_removes_all_nodes(void)
{
    TestList list = TestList__new();
    TestList__push(&list, 10);
    TestList__push(&list, 20);

    match_result(CollectionCode, TestList__dispose(&list), success_do_nothing(), error_test_fail());

    TEST_ASSERT_EQUAL(0, TestList__len(&list));
    TEST_ASSERT_NULL(list.head);
    TEST_ASSERT_NULL(list.tail);
}
