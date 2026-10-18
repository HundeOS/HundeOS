#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "../../../user/lib/arch/riscv64/compiler_runtime.h"

#define DOUBLE_EXPONENT_MASK (UINT64_C(0x7ff) << 52)
#define DOUBLE_FRACTION_MASK ((UINT64_C(1) << 52) - 1)

typedef union TestDoubleRepresentation
{
    double value;
    uint64_t bits;
} TestDoubleRepresentation;

typedef double (*BinaryDoubleOperation)(double left, double right);
typedef int (*CompareDoubleOperation)(double left, double right);

static uint64_t random_state;

void setUp(void)
{
    random_state = UINT64_C(0x9e3779b97f4a7c15);
}

void tearDown(void)
{
}

static bool bits_are_nan(uint64_t bits)
{
    return (bits & DOUBLE_EXPONENT_MASK) == DOUBLE_EXPONENT_MASK && (bits & DOUBLE_FRACTION_MASK) != 0;
}

static uint64_t next_random_bits(void)
{
    random_state ^= random_state >> 12;
    random_state ^= random_state << 25;
    random_state ^= random_state >> 27;
    return random_state * UINT64_C(0x2545f4914f6cdd1d);
}

static double hardware_add(double left, double right)
{
    return left + right;
}

static double hardware_subtract(double left, double right)
{
    return left - right;
}

static double hardware_multiply(double left, double right)
{
    return left * right;
}

static double hardware_divide(double left, double right)
{
    return left / right;
}

static void assert_same_result(BinaryDoubleOperation expected_operation, BinaryDoubleOperation actual_operation,
                               uint64_t left_bits, uint64_t right_bits)
{
    TestDoubleRepresentation left = {.bits = left_bits};
    TestDoubleRepresentation right = {.bits = right_bits};
    TestDoubleRepresentation expected = {.value = expected_operation(left.value, right.value)};
    TestDoubleRepresentation actual = {.value = actual_operation(left.value, right.value)};
    if (bits_are_nan(expected.bits))
    {
        TEST_ASSERT_TRUE(bits_are_nan(actual.bits));
        return;
    }
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(expected.bits >> 32), (uint32_t)(actual.bits >> 32));
    TEST_ASSERT_EQUAL_HEX32((uint32_t)expected.bits, (uint32_t)actual.bits);
}

static void assert_edge_cases(BinaryDoubleOperation expected_operation, BinaryDoubleOperation actual_operation)
{
    static const uint64_t values[] = {
        UINT64_C(0x0000000000000000), UINT64_C(0x8000000000000000), UINT64_C(0x0000000000000001),
        UINT64_C(0x000fffffffffffff), UINT64_C(0x0010000000000000), UINT64_C(0x3ca0000000000000),
        UINT64_C(0x3ff0000000000000), UINT64_C(0x4000000000000000), UINT64_C(0x7fefffffffffffff),
        UINT64_C(0x8000000000000001), UINT64_C(0xbff0000000000000), UINT64_C(0xffefffffffffffff),
        UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8000000000001),
    };
    for (size_t left = 0; left < sizeof(values) / sizeof(values[0]); left++)
    {
        for (size_t right = 0; right < sizeof(values) / sizeof(values[0]); right++)
        {
            assert_same_result(expected_operation, actual_operation, values[left], values[right]);
        }
    }
}

static void assert_random_cases(BinaryDoubleOperation expected_operation, BinaryDoubleOperation actual_operation)
{
    for (size_t index = 0; index < 100000; index++)
    {
        assert_same_result(expected_operation, actual_operation, next_random_bits(), next_random_bits());
    }
}

static void assert_integer_conversions(uint64_t bits)
{
    TestDoubleRepresentation expected_signed = {.value = (double)(int64_t)bits};
    TestDoubleRepresentation actual_signed = {.value = __floatdidf((int64_t)bits)};
    TestDoubleRepresentation expected_unsigned = {.value = (double)bits};
    TestDoubleRepresentation actual_unsigned = {.value = __floatundidf(bits)};
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(expected_signed.bits >> 32), (uint32_t)(actual_signed.bits >> 32));
    TEST_ASSERT_EQUAL_HEX32((uint32_t)expected_signed.bits, (uint32_t)actual_signed.bits);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(expected_unsigned.bits >> 32), (uint32_t)(actual_unsigned.bits >> 32));
    TEST_ASSERT_EQUAL_HEX32((uint32_t)expected_unsigned.bits, (uint32_t)actual_unsigned.bits);
}

static void assert_comparison(CompareDoubleOperation operation, uint64_t left_bits, uint64_t right_bits,
                              int expected_nan_result)
{
    TestDoubleRepresentation left = {.bits = left_bits};
    TestDoubleRepresentation right = {.bits = right_bits};
    int actual = operation(left.value, right.value);
    if (bits_are_nan(left_bits) || bits_are_nan(right_bits))
    {
        TEST_ASSERT_EQUAL_INT(expected_nan_result, actual);
    }
    else if (left.value < right.value)
    {
        TEST_ASSERT_LESS_THAN_INT(0, actual);
    }
    else if (left.value > right.value)
    {
        TEST_ASSERT_GREATER_THAN_INT(0, actual);
    }
    else
    {
        TEST_ASSERT_EQUAL_INT(0, actual);
    }
}

void test_adddf3_matches_hardware_binary64(void)
{
    assert_edge_cases(hardware_add, __adddf3);
    assert_random_cases(hardware_add, __adddf3);
}

void test_subdf3_matches_hardware_binary64(void)
{
    assert_edge_cases(hardware_subtract, __subdf3);
    assert_random_cases(hardware_subtract, __subdf3);
}

void test_muldf3_matches_hardware_binary64(void)
{
    assert_edge_cases(hardware_multiply, __muldf3);
    assert_random_cases(hardware_multiply, __muldf3);
}

void test_divdf3_matches_hardware_binary64(void)
{
    assert_edge_cases(hardware_divide, __divdf3);
    assert_random_cases(hardware_divide, __divdf3);
}

void test_integer_to_binary64_conversions_match_hardware(void)
{
    static const uint64_t values[] = {
        0,
        1,
        UINT32_MAX,
        UINT64_C(0x7fffffff),
        UINT64_C(0x80000000),
        UINT64_C(0x7fffffffffffffff),
        UINT64_C(0x8000000000000000),
        UINT64_MAX,
        UINT64_C(0x0020000000000001),
    };
    for (size_t index = 0; index < sizeof(values) / sizeof(values[0]); index++)
    {
        assert_integer_conversions(values[index]);
    }
    for (size_t index = 0; index < 100000; index++)
    {
        assert_integer_conversions(next_random_bits());
    }

    TestDoubleRepresentation signed_32 = {.value = __floatsidf(INT32_MIN)};
    TestDoubleRepresentation unsigned_32 = {.value = __floatunsidf(UINT32_MAX)};
    TestDoubleRepresentation expected_signed_32 = {.value = (double)INT32_MIN};
    TestDoubleRepresentation expected_unsigned_32 = {.value = (double)UINT32_MAX};
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(expected_signed_32.bits >> 32), (uint32_t)(signed_32.bits >> 32));
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(expected_unsigned_32.bits >> 32), (uint32_t)(unsigned_32.bits >> 32));
}

void test_binary64_comparisons_match_hardware_order(void)
{
    for (size_t index = 0; index < 100000; index++)
    {
        uint64_t left = next_random_bits();
        uint64_t right = next_random_bits();
        assert_comparison(__eqdf2, left, right, 1);
        assert_comparison(__ltdf2, left, right, 1);
        assert_comparison(__ledf2, left, right, 1);
        assert_comparison(__gtdf2, left, right, -1);
        assert_comparison(__gedf2, left, right, -1);
        TEST_ASSERT_EQUAL_INT(bits_are_nan(left) || bits_are_nan(right),
                              __unorddf2((TestDoubleRepresentation){.bits = left}.value,
                                         (TestDoubleRepresentation){.bits = right}.value));
    }
}
