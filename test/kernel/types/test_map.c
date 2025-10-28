#include <stdint.h>

#include "unity.h"
#include <stdint.h>

#include "../../../kernel/types/map.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/types/result.h"
#include "../../../kernel/types/comparison.h"
#include "../../../kernel/types/kstring.h"
#include "../../../kernel/types/kstring_optionals.h"

typedef uint64_t TestInt;

OptionalDef(TestInt);

MapDefine(KString, TestInt, 32, TestMap, kstring_equals);
MapBodyDefine(TestMap);

MapDefine(KString, TestInt, 1, TinyTestMap, kstring_equals);
MapBodyDefine(TinyTestMap);

void setUp()
{

}

void tearDown()
{

}

void test_map_set_and_get()
{
    TestMap map = TestMap__new();

    ResultMatch(MapCode, TestMap__set(&map, kstring("hello"), 12));
        IsError(code, message, TEST_FAIL());
    End

    Match(TestInt, TestMap__get(&map, kstring("hello")));
        IsNone(TEST_FAIL());
        IsSome(value, TEST_ASSERT_EQUAL_INT(12, value))
    End
}

void test_map_set_and_delete()
{
    TestMap map = TestMap__new();

    ResultMatch(MapCode, TestMap__set(&map, kstring("hello"), 12));
        IsError(code, message, TEST_FAIL());
    End

    Match(TestInt, TestMap__get(&map, kstring("hello")));
        IsNone(TEST_FAIL());
        IsSome(value, TEST_ASSERT_EQUAL_INT(12, value))
    End

    ResultMatch(MapCode, TestMap__delete(&map, kstring("hello")));
        IsError(code, message, TEST_FAIL());
    End

    Match(TestInt, TestMap__get(&map, kstring("hello")));
        IsSome(value, TEST_FAIL())
    End

}

void test_map_delete_only_deletes_requested_entry()
{
    TestMap map = TestMap__new();
    KString key1 = kstring("hello");
    KString key2 = kstring("bye bye");
    KString key3 = kstring("i'm here");

    TestMap__set(&map, key1, 11);
    TestMap__set(&map, key2, 12);
    TestMap__set(&map, key3, 13);


    ResultMatch(MapCode, TestMap__delete(&map, key2));
        IsError(code, message, TEST_FAIL());
    End

    Match(TestInt, TestMap__get(&map, key2));
        IsSome(value, TEST_FAIL())
    End
    Match(TestInt, TestMap__get(&map, key1));
        IsNone(TEST_FAIL())
    End
    Match(TestInt, TestMap__get(&map, key3));
        IsNone(TEST_FAIL())
    End

}

void test_map_get_with_non_existent_key()
{
    TestMap map = TestMap__new();

    Match(TestInt, TestMap__get(&map, kstring("hello")));
        IsSome(value, TEST_FAIL())
    End
}

void test_map_set_with_full_map()
{
    TinyTestMap map = TinyTestMap__new();

    ResultMatch(MapCode, TinyTestMap__set(&map, kstring("hello"), 12));
        IsError(code, message, TEST_FAIL());
    End
    ResultMatch(MapCode, TinyTestMap__set(&map, kstring("goodbye"), 12));
        IsSuccess(val, TEST_FAIL());
    End
}
