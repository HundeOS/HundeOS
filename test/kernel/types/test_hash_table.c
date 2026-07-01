#include "unity.h"
#include <stdint.h>

#include "../../../kernel/types/hashtable.h"

typedef uint64_t TestKey;
typedef uint64_t TestValue;

OptionalDef(TestKey);
OptionalDef(TestValue);

HashCode hash_uint64(uint64_t * key)
{
    return (HashCode) *key;
}

HashtableDefine(TestKey, TestValue, 128, 1024, TestHashTable, hash_uint64, SimpleEquals(uint64_t), SimpleEquals(uint64_t))
HashtableBodyDefine(TestHashTable)

void setUp(void)
{

}

void tearDown(void)
{

}

void test_hash_table_insert_and_lookup(void)
{
    TestHashTable table = TestHashTable__new();
    TestHashTable__set(&table, 123, 456);
    Optional(TestValue) value_opt = TestHashTable__get(&table, 123);
    TestValue value = Unwrap(TestValue, value_opt, (TestValue){});
    TEST_ASSERT_EQUAL_INT(456, value);
}

void test_hash_table_insert_and_update(void)
{
    TestHashTable table = TestHashTable__new();
    TestHashTable__set(&table, 123, 456);
    TestHashTable__set(&table, 123, 789);
    Optional(TestValue) value_opt = TestHashTable__get(&table, 123);
    TestValue value = Unwrap(TestValue, value_opt, (TestValue){});
    TEST_ASSERT_EQUAL_INT(789, value);
}
