#include <stdint.h>

#include "unity.h"

#include "shared_map.h"

typedef uint64_t TestInt;

OptionalDef(TestInt);

static bool bstring_key_equals(void * a, void * b)
{
    return BString__equals((BStringPtr)a, (BStringPtr)b);
}

MapDefine(BString, TestInt, 32, TestMap, &bstring_key_equals);
MapBodyDefine(TestMap);

MapDefine(BString, TestInt, 1, TinyTestMap, &bstring_key_equals);
MapBodyDefine(TinyTestMap);

void test_map_len_key_set_get_and_delete(void)
{
    TestMap map = TestMap__new();
    BString hello = BString__new("hello");
    BString world = BString__new("world");

    match_result(TestMap__set(&map, hello, 12),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(TestMap__set(&map, world, 13),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(2, TestMap__len(&map));
    TEST_ASSERT_TRUE(BString__equals(&hello, unwrap_optional(TestMap__key(&map, 0), (BStringPtr){})));

    match_optional(TestMap__get(&map, hello),
        some(value) { TEST_ASSERT_EQUAL(12, value); },
        none_test_fail()
    );

    match_result(TestMap__delete(&map, hello),
        success_do_nothing(),
        error_test_fail()
    );

    match_optional(TestMap__get(&map, hello),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_map_full_gives_error(void)
{
    TinyTestMap map = TinyTestMap__new();

    match_result(TinyTestMap__set(&map, BString__new("hello"), 12),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(
        TinyTestMap__set(&map, BString__new("goodbye"), 13),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(MAP_FULL, code); }
    );
}

void test_map_ptr_iterator_exposes_key_and_value_pointers(void)
{
    TestMap map = TestMap__new();
    BString hello = BString__new("hello");
    BString world = BString__new("world");
    uint64_t key_count = 0;

    match_result(TestMap__set(&map, hello, 12),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(TestMap__set(&map, world, 13),
        success_do_nothing(),
        error_test_fail()
    );

    for_each_map_ptr(TestMap, &map, index, key, value,
                     TEST_ASSERT_TRUE(BString__equals(key, &hello) || BString__equals(key, &world));
                     *value += 10; key_count++;);

    TEST_ASSERT_EQUAL(2, key_count);
    match_optional(TestMap__get(&map, hello),
        some(value) { TEST_ASSERT_EQUAL(22, value); },
        none_test_fail()
    );
    match_optional(TestMap__get(&map, world),
        some(value) { TEST_ASSERT_EQUAL(23, value); },
        none_test_fail()
    );
}
