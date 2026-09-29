#include <stdint.h>

#include "unity.h"

#include "../../../common/types/cache.h"

typedef struct TestCacheValue
{
    uint64_t value;
} TestCacheValue;

CacheDefine(uint64_t, TestCacheValue, 3U, TestCache);

static bool TestCache__keys_equal(const uint64_t * first, const uint64_t * second)
{
    return *first == *second;
}

CacheBodyDefine(TestCache, TestCache__keys_equal);

static TestCacheValue value(uint64_t number)
{
    return (TestCacheValue){.value = number};
}

void test_cache_put_get_update_and_len(void)
{
    TestCache cache = TestCache__new();

    TEST_ASSERT_EQUAL(CACHE_INSERTED, TestCache__put(&cache, 1, value(10)));
    TEST_ASSERT_EQUAL(CACHE_UPDATED, TestCache__put(&cache, 1, value(11)));
    TEST_ASSERT_EQUAL_UINT64(1, TestCache__len(&cache));
    match_optional(
        TestCache__get(&cache, 1),
        some(found)
        {
            TEST_ASSERT_EQUAL_UINT64(11, found.value);
        },
        none_test_fail()
    );
}

void test_cache_preserves_frequent_entry_during_scan(void)
{
    TestCache cache = TestCache__new();
    TestCache__put(&cache, 1, value(10));
    TestCache__put(&cache, 2, value(20));
    TestCache__put(&cache, 3, value(30));
    TEST_ASSERT_EQUAL_UINT64(10, unwrap_optional(TestCache__get(&cache, 1), value(0)).value);

    TEST_ASSERT_EQUAL(CACHE_EVICTED, TestCache__put(&cache, 4, value(40)));
    TEST_ASSERT_EQUAL(CACHE_EVICTED, TestCache__put(&cache, 5, value(50)));
    TEST_ASSERT_EQUAL_UINT64(10, unwrap_optional(TestCache__get(&cache, 1), value(0)).value);
    match_optional(TestCache__get(&cache, 2),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_cache_evicts_oldest_frequent_entry_when_all_are_frequent(void)
{
    TestCache cache = TestCache__new();
    TestCache__put(&cache, 1, value(10));
    TestCache__put(&cache, 2, value(20));
    TestCache__put(&cache, 3, value(30));
    TestCache__get(&cache, 1);
    TestCache__get(&cache, 2);
    TestCache__get(&cache, 3);

    TEST_ASSERT_EQUAL(CACHE_EVICTED, TestCache__put(&cache, 4, value(40)));
    match_optional(TestCache__get(&cache, 1),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_EQUAL_UINT64(20, unwrap_optional(TestCache__get(&cache, 2), value(0)).value);
}

void test_cache_replace_does_not_admit_and_invalidate_removes(void)
{
    TestCache cache = TestCache__new();

    TEST_ASSERT_FALSE(TestCache__replace(&cache, 1, value(10)));
    TEST_ASSERT_EQUAL_UINT64(0, TestCache__len(&cache));
    TestCache__put(&cache, 1, value(10));
    TEST_ASSERT_TRUE(TestCache__replace(&cache, 1, value(11)));
    TEST_ASSERT_TRUE(TestCache__invalidate(&cache, 1));
    TEST_ASSERT_FALSE(TestCache__invalidate(&cache, 1));
    TEST_ASSERT_EQUAL_UINT64(0, TestCache__len(&cache));
}
