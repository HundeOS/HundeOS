#include <stdint.h>

#include "unity.h"

#include "../../../user/lib/types/map.h"

typedef uint64_t TestType;
typedef uint64_t TestKey;

OptionalDef(TestType);
OptionalDef(TestKey);

static bool test_key_equals(void * a, void * b)
{
    return *(uint64_t *)a == *(uint64_t *)b;
}

MapDefine(TestKey, TestType, 1, TinyTestMap, &test_key_equals);
MapBodyDefine(TinyTestMap);

void test_user_map_wrapper_returns_full_error(void)
{
    TinyTestMap map = TinyTestMap__new();

    match_result(TinyTestMap__set(&map, 1, 11),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(
        TinyTestMap__set(&map, 2, 12),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(MAP_FULL, code);
        }
    );
}
