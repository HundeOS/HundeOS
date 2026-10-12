#include <stdint.h>

#include "unity.h"

#include "../../../arch/riscv64/mmio_load.h"

static ProcessContext context;

void setUp(void)
{
    context = (ProcessContext){0};
}

void tearDown(void)
{
}

static RiscVMMIOLoad decode(uint32_t instruction)
{
    return unwrap_optional(RiscVMMIOLoad__decode(instruction, &context), (RiscVMMIOLoad){0});
}

void test_mmio_load_decodes_standard_signed_word_load(void)
{
    context.x[15] = 0x40003000;
    uint32_t instruction = (0x84U << 20U) | (15U << 15U) | (2U << 12U) | (14U << 7U) | 0x03U;

    RiscVMMIOLoad load = decode(instruction);

    TEST_ASSERT_EQUAL_UINT64(0x40003084, load.address);
    TEST_ASSERT_EQUAL_UINT32(14, load.destination_register);
    TEST_ASSERT_EQUAL_UINT64(4, load.width);
    TEST_ASSERT_TRUE(load.sign_extend);
    TEST_ASSERT_EQUAL_UINT64(4, load.instruction_size);
}

void test_mmio_load_decodes_standard_unsigned_word_load(void)
{
    context.x[8] = 0x40001000;
    uint32_t instruction = (0xffcU << 20U) | (8U << 15U) | (6U << 12U) | (9U << 7U) | 0x03U;

    RiscVMMIOLoad load = decode(instruction);

    TEST_ASSERT_EQUAL_UINT64(0x40000ffc, load.address);
    TEST_ASSERT_EQUAL_UINT64(4, load.width);
    TEST_ASSERT_FALSE(load.sign_extend);
}

void test_mmio_load_decodes_emitted_compressed_word_load(void)
{
    context.x[15] = 0x40003084;

    RiscVMMIOLoad load = decode(0x4394U);

    TEST_ASSERT_EQUAL_UINT64(0x40003084, load.address);
    TEST_ASSERT_EQUAL_UINT32(13, load.destination_register);
    TEST_ASSERT_EQUAL_UINT64(4, load.width);
    TEST_ASSERT_TRUE(load.sign_extend);
    TEST_ASSERT_EQUAL_UINT64(2, load.instruction_size);
}

void test_mmio_load_performs_signed_load_at_access_address_and_advances_pc(void)
{
    volatile uint32_t register_value = 0x80000001U;
    context.sepc = 0x1000;
    RiscVMMIOLoad load = RiscVMMIOLoad__new(0x40003084, 13, sizeof(uint32_t), true, sizeof(uint16_t));

    RiscVMMIOLoad__perform(&load, (Address)&register_value, &context);

    TEST_ASSERT_EQUAL_UINT64(0xffffffff80000001ULL, context.x[13]);
    TEST_ASSERT_EQUAL_UINT64(0x1002, context.sepc);
}

void test_mmio_load_rejects_non_load_instruction(void)
{
    TEST_ASSERT_TRUE(is_nothing(RiscVMMIOLoad__decode(0x00000013U, &context)));
}
