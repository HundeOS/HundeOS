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

    ResultMatch(CollectionCode, TinyTestMap__set(&map, 1, 11))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(CollectionCode, TinyTestMap__set(&map, 2, 12))
        IsSuccess(index, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(MAP_FULL, code))
    End
}
