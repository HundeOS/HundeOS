#include <stdint.h>

#include "unity.h"

#include "shared_map.h"

typedef uint64_t TestInt;

OptionalDef(TestInt);

static bool bstring_key_equals(void * a, void * b)
{
    return BString__equals((BStringPtr) a, (BStringPtr) b);
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

    ResultMatch(CollectionCode, TestMap__set(&map, hello, 12))
        IsError(code, message, TEST_FAIL())
    End
    ResultMatch(CollectionCode, TestMap__set(&map, world, 13))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(2, TestMap__len(&map));
    TEST_ASSERT_TRUE(BString__equals(&hello, Unwrap(BStringPtr, TestMap__key(&map, 0), (BStringPtr){})));

    Match(TestInt, TestMap__get(&map, hello))
        IsNone(TEST_FAIL())
        IsSome(value, TEST_ASSERT_EQUAL(12, value))
    End

    ResultMatch(CollectionCode, TestMap__delete(&map, hello))
        IsError(code, message, TEST_FAIL())
    End

    Match(TestInt, TestMap__get(&map, hello))
        IsSome(value, TEST_FAIL())
    End
}

void test_map_full_gives_error(void)
{
    TinyTestMap map = TinyTestMap__new();

    ResultMatch(CollectionCode, TinyTestMap__set(&map, BString__new("hello"), 12))
        IsError(code, message, TEST_FAIL())
    End
    ResultMatch(CollectionCode, TinyTestMap__set(&map, BString__new("goodbye"), 13))
        IsSuccess(value, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(MAP_FULL, code))
    End
}
