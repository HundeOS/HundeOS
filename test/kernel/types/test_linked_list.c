#include <stdint.h>

#include "unity.h"

#include "../../../kernel/types/comparison.h"
#include "../../../kernel/types/linked_list.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/types/result.h"

typedef uint64_t TestListType;
typedef TestListType * TestListTypePtr;

OptionalDef(TestListType);

LinkedListDefine(TestListType, TestList, SimpleEquals(uint64_t));
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

    ResultMatch(CollectionCode, TestList__push(&list, 10))
        IsError(code, message, TEST_FAIL())
        IsSuccess(index, TEST_ASSERT_EQUAL(0, index))
    End

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

    LLFindResultMatch(TestList, TestList__find(&list, value_matches, &expected))
        IsNone(TEST_FAIL())
        IsSome(found,
        {
            TEST_ASSERT_EQUAL(1, found.index);
            TEST_ASSERT_EQUAL(20, *found.value);
        })
    End
}

void test_linked_list_iterate_visits_items_in_order(void)
{
    TestList list = TestList__new();
    TestList__push(&list, 10);
    TestList__push(&list, 20);
    TestList__push(&list, 30);

    TestListType expected = 10;
    uint64_t count = 0;

    LLIterate(TestList, list, value)
        TEST_ASSERT_EQUAL(expected, value);
        expected += 10;
        count++;
    End

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

    ResultMatch(CollectionCode, TestList__remove(&list, 20))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(2, TestList__len(&list));
    TEST_ASSERT_EQUAL(30, Unwrap(TestListType, TestList__at(&list, 1), (TestListType){}));
}

void test_linked_list_dispose_removes_all_nodes(void)
{
    TestList list = TestList__new();
    TestList__push(&list, 10);
    TestList__push(&list, 20);

    ResultMatch(CollectionCode, TestList__dispose(&list))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(0, TestList__len(&list));
    TEST_ASSERT_NULL(list.head);
    TEST_ASSERT_NULL(list.tail);
}
