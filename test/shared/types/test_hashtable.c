#include <stdint.h>

#include "unity.h"

#include "shared_hashtable.h"

typedef uint64_t TestKey;
typedef uint64_t TestValue;

OptionalDef(TestKey);
OptionalDef(TestValue);

static bool test_value_equals(void * first, void * second)
{
    return *(TestValue *)first == *(TestValue *)second;
}

static bool test_key_equals(void * first, void * second)
{
    return *(TestKey *)first == *(TestKey *)second;
}

static HashCode test_hash(TestKey * key)
{
    return *key;
}

HashtableDefine(TestKey, TestValue, 2, 3, TestHashtable, &test_hash, &test_key_equals, &test_value_equals);
HashtableBodyDefine(TestHashtable);

void test_hashtable_init_resets_existing_storage(void)
{
    TestHashtable table = TestHashtable__new();
    match_result(TestHashtable__set(&table, 1, 10),
        success_do_nothing(),
        error_test_fail()
    );

    TestHashtable__init(&table);

    TEST_ASSERT_EQUAL_UINT64(0, TestHashtable__len(&table));
    TEST_ASSERT_TRUE(is_nothing(TestHashtable__get(&table, 1)));
}

void test_hashtable_insert_lookup_replace_and_len(void)
{
    TestHashtable table = TestHashtable__new();

    match_result(TestHashtable__set(&table, 1, 10),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(TestHashtable__set(&table, 3, 30),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(TestHashtable__set(&table, 1, 11),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(2, TestHashtable__len(&table));
    TEST_ASSERT_EQUAL_UINT64(11, unwrap_optional(TestHashtable__get(&table, 1), (TestValue){}));
    TEST_ASSERT_EQUAL_UINT64(30, unwrap_optional(TestHashtable__get(&table, 3), (TestValue){}));
}

void test_hashtable_iterates_colliding_entries(void)
{
    TestHashtable table = TestHashtable__new();
    match_result(TestHashtable__set(&table, 1, 10),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(TestHashtable__set(&table, 3, 30),
        success_do_nothing(),
        error_test_fail()
    );

    uint64_t count = 0;
    uint64_t total = 0;
    for_each_hashtable(
        TestHashtable,
        table,
        bucket_index,
        entry_index,
        key,
        value,
                       count++;
                   total += key + value;
    )
    ;

    TEST_ASSERT_EQUAL_UINT64(2, count);
    TEST_ASSERT_EQUAL_UINT64(44, total);
}

void test_hashtable_ptr_iterator_updates_values(void)
{
    TestHashtable table = TestHashtable__new();
    match_result(TestHashtable__set(&table, 2, 20),
        success_do_nothing(),
        error_test_fail()
    );

    for_each_hashtable_ptr(
        TestHashtable,
        &table,
        bucket_index,
        entry_index,
        key,
        value,
                           TEST_ASSERT_EQUAL_UINT64(2, *key);
                       *value = 25;
    )
    ;

    TEST_ASSERT_EQUAL_UINT64(25, unwrap_optional(TestHashtable__get(&table, 2), (TestValue){}));
}

void test_hashtable_reports_full_collision_bucket(void)
{
    TestHashtable table = TestHashtable__new();
    match_result(TestHashtable__set(&table, 1, 10),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(TestHashtable__set(&table, 3, 30),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(TestHashtable__set(&table, 5, 50),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(
        TestHashtable__set(&table, 7, 70),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(ARRAY_FULL, code);
        }
    );
}

void test_hashtable_delete_updates_len_and_removes_empty_bucket(void)
{
    TestHashtable table = TestHashtable__new();
    match_result(TestHashtable__set(&table, 1, 10),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(TestHashtable__set(&table, 3, 30),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(TestHashtable__delete(&table, 1),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(1, TestHashtable__len(&table));
    TEST_ASSERT_TRUE(is_nothing(TestHashtable__get(&table, 1)));
    TEST_ASSERT_EQUAL_UINT64(30, unwrap_optional(TestHashtable__get(&table, 3), (TestValue){}));

    match_result(TestHashtable__delete(&table, 3),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(0, TestHashtable__len(&table));
    TEST_ASSERT_TRUE(is_nothing(TestHashtable__get(&table, 3)));

    match_result(TestHashtable__delete(&table, 99),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(0, TestHashtable__len(&table));
}

void test_hashtable_delete_does_not_relocate_another_hash_bucket(void)
{
    TestHashtable table = TestHashtable__new();
    match_result(TestHashtable__set(&table, 1, 10),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(TestHashtable__set(&table, 2, 20),
        success_do_nothing(),
        error_test_fail()
    );

    TestValue * value_before = unwrap_optional(TestHashtable__get_ptr(&table, 2), (TestValue *){});
    match_result(TestHashtable__delete(&table, 1),
        success_do_nothing(),
        error_test_fail()
    );
    TestValue * value_after = unwrap_optional(TestHashtable__get_ptr(&table, 2), (TestValue *){});

    TEST_ASSERT_EQUAL_PTR(value_before, value_after);
    TEST_ASSERT_EQUAL_UINT64(20, *value_after);
}
