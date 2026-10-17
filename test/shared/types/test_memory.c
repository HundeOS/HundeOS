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

void test_memory_copy_handles_alignment_and_tail_lengths(void)
{
    uint8_t source[128];
    uint8_t destination[128];
    for (size_t index = 0; index < sizeof(source); index++)
    {
        source[index] = (uint8_t)(index ^ 0x5aU);
    }

    for (size_t source_offset = 0; source_offset < 16; source_offset++)
    {
        for (size_t destination_offset = 0; destination_offset < 16; destination_offset++)
        {
            for (size_t length = 0; length <= 96; length++)
            {
                Mem__memset(destination, 0xa5, sizeof(destination));
                TEST_ASSERT_EQUAL_PTR(destination + destination_offset,
                                      Mem__memcpy(destination + destination_offset, source + source_offset, length));
                for (size_t index = 0; index < sizeof(destination); index++)
                {
                    uint8_t expected = 0xa5;
                    if (index >= destination_offset && index < destination_offset + length)
                    {
                        expected = source[source_offset + index - destination_offset];
                    }
                    TEST_ASSERT_EQUAL_UINT8(expected, destination[index]);
                }
            }
        }
    }
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

void test_memory_set_handles_unaligned_ranges_and_tail_lengths(void)
{
    uint8_t bytes[96];
    for (size_t offset = 0; offset < 16; offset++)
    {
        for (size_t length = 0; length <= 64; length++)
        {
            for (size_t index = 0; index < sizeof(bytes); index++)
            {
                bytes[index] = 0xa5;
            }

            TEST_ASSERT_EQUAL_PTR(bytes + offset, Mem__memset(bytes + offset, 0x3c, length));
            for (size_t index = 0; index < sizeof(bytes); index++)
            {
                uint8_t expected = index >= offset && index < offset + length ? 0x3c : 0xa5;
                TEST_ASSERT_EQUAL_UINT8(expected, bytes[index]);
            }
        }
    }
}

void test_memory_compare_uses_unsigned_byte_order(void)
{
    const uint8_t lower[] = {1, 0x7f};
    const uint8_t higher[] = {1, 0x80};

    TEST_ASSERT_EQUAL_INT(0, Mem__memcmp(lower, lower, sizeof(lower)));
    TEST_ASSERT_LESS_THAN_INT(0, Mem__memcmp(lower, higher, sizeof(lower)));
    TEST_ASSERT_GREATER_THAN_INT(0, Mem__memcmp(higher, lower, sizeof(lower)));
}
