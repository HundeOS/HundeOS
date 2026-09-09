#include <stdint.h>

#include "unity.h"

#include "../../../kernel/types/map.h"
#include "../../../kernel/types/comparison.h"

typedef uint64_t TestType;
typedef uint64_t TestKey;

OptionalDef(TestType);
OptionalDef(TestKey);

MapDefine(TestKey, TestType, 1, TinyTestMap, SimpleEquals(uint64_t));
MapBodyDefine(TinyTestMap);

void test_kernel_map_wrapper_returns_full_error(void)
{
    TinyTestMap map = TinyTestMap__new();

    match_result(TinyTestMap__set(&map, 1, 11),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(
        TinyTestMap__set(&map, 2, 12),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(MAP_FULL, code); }
    );
}
