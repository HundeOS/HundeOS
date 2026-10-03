#include <stdint.h>

#include "unity.h"

#include "../../../common/memory.h"

void test_memory_copy_copies_bytes_and_returns_destination(void)
{
    const uint8_t source[] = {1, 2, 3, 4};
    uint8_t destination[] = {0, 0, 0, 0};

    TEST_ASSERT_EQUAL_PTR(destination, Mem__memcpy(destination, source, sizeof(source)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(source, destination, sizeof(source));
}

void test_memory_move_supports_overlapping_regions(void)
{
    uint8_t bytes[] = {1, 2, 3, 4, 5, 6};
    const uint8_t expected[] = {1, 2, 1, 2, 3, 4};

    TEST_ASSERT_EQUAL_PTR(bytes + 2, Mem__memmove(bytes + 2, bytes, 4));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bytes, sizeof(bytes));
}

void test_memory_set_fills_bytes_and_returns_destination(void)
{
    uint8_t bytes[] = {1, 2, 3, 4};
    const uint8_t expected[] = {0xa5, 0xa5, 0xa5, 0xa5};

    TEST_ASSERT_EQUAL_PTR(bytes, Mem__memset(bytes, 0xa5, sizeof(bytes)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bytes, sizeof(bytes));
}

void test_memory_compare_uses_unsigned_byte_order(void)
{
    const uint8_t lower[] = {1, 0x7f};
    const uint8_t higher[] = {1, 0x80};

    TEST_ASSERT_EQUAL_INT(0, Mem__memcmp(lower, lower, sizeof(lower)));
    TEST_ASSERT_LESS_THAN_INT(0, Mem__memcmp(lower, higher, sizeof(lower)));
    TEST_ASSERT_GREATER_THAN_INT(0, Mem__memcmp(higher, lower, sizeof(lower)));
}
