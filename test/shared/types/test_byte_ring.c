#include <stdint.h>

#include "unity.h"

#include "../../../common/types/byte_ring.h"

void test_byte_ring_preserves_bytes_across_wrap(void)
{
    uint8_t storage[5];
    ByteRing ring = ByteRing__new(storage, sizeof(storage));
    uint8_t first[] = {1, 2, 3, 4};
    uint8_t second[] = {5, 6, 7};
    uint8_t output[5];

    TEST_ASSERT_EQUAL_UINT64(4, unwrap_result(ByteRing__push(&ring, first, sizeof(first)), 0));
    TEST_ASSERT_EQUAL_UINT64(2, ByteRing__drain(&ring, output, 2));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first, output, 2);
    TEST_ASSERT_EQUAL_UINT64(3, unwrap_result(ByteRing__push(&ring, second, sizeof(second)), 0));
    TEST_ASSERT_EQUAL_UINT64(5, ByteRing__drain(&ring, output, sizeof(output)));

    uint8_t expected[] = {3, 4, 5, 6, 7};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, output, sizeof(expected));
}

void test_byte_ring_rejects_a_write_larger_than_free_space(void)
{
    uint8_t storage[3];
    ByteRing ring = ByteRing__new(storage, sizeof(storage));
    uint8_t bytes[] = {1, 2, 3, 4};

    match_result(
        ByteRing__push(&ring, bytes, sizeof(bytes)),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL_INT(-1, code);
        }
    );
    TEST_ASSERT_EQUAL_UINT64(0, ByteRing__count(&ring));
}

void test_byte_ring_zero_length_operations_support_zero_capacity(void)
{
    ByteRing ring = ByteRing__new(NULL, 0);

    TEST_ASSERT_EQUAL_UINT64(0, unwrap_result(ByteRing__push(&ring, NULL, 0), 1));
    TEST_ASSERT_EQUAL_UINT64(0, ByteRing__drain(&ring, NULL, 0));
}
