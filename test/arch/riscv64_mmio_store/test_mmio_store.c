#include <stdint.h>

#include "unity.h"

#include "../../../arch/riscv64/mmio_store.h"

static ProcessContext context;

void setUp(void)
{
    context = (ProcessContext){0};
}

void tearDown(void)
{
}

static RiscVMMIOStore decode(uint32_t instruction)
{
    return unwrap_optional(RiscVMMIOStore__decode(instruction, &context), (RiscVMMIOStore){0});
}

void test_mmio_store_decodes_standard_word_store(void)
{
    context.x[15] = 0x40003000;
    context.x[14] = 0x12345678abcdef01ULL;
    uint32_t immediate = 0x84U;
    uint32_t instruction = ((immediate >> 5U) << 25U) | (14U << 20U) | (15U << 15U) | (2U << 12U) |
                           ((immediate & 0x1fU) << 7U) | 0x23U;

    RiscVMMIOStore store = decode(instruction);

    TEST_ASSERT_EQUAL_UINT64(0x40003084, store.address);
    TEST_ASSERT_EQUAL_UINT64(context.x[14], store.value);
    TEST_ASSERT_EQUAL_UINT64(4, store.width);
    TEST_ASSERT_EQUAL_UINT64(4, store.instruction_size);
}

void test_mmio_store_decodes_negative_standard_offset(void)
{
    context.x[8] = 0x40001000;
    context.x[9] = 0x55;
    uint32_t immediate = 0xff8U;
    uint32_t instruction = ((immediate >> 5U) << 25U) | (9U << 20U) | (8U << 15U) | (3U << 12U) |
                           ((immediate & 0x1fU) << 7U) | 0x23U;

    RiscVMMIOStore store = decode(instruction);

    TEST_ASSERT_EQUAL_UINT64(0x40000ff8, store.address);
    TEST_ASSERT_EQUAL_UINT64(8, store.width);
}

void test_mmio_store_decodes_compressed_word_store(void)
{
    context.x[15] = 0x40003084;
    context.x[14] = 0x1122334455667788ULL;
    uint16_t instruction = 0xc398U;

    RiscVMMIOStore store = decode(instruction);

    TEST_ASSERT_EQUAL_UINT64(0x40003084, store.address);
    TEST_ASSERT_EQUAL_UINT64(context.x[14], store.value);
    TEST_ASSERT_EQUAL_UINT64(4, store.width);
    TEST_ASSERT_EQUAL_UINT64(2, store.instruction_size);
}

void test_mmio_store_decodes_compressed_double_store(void)
{
    context.x[10] = 0x50000000;
    context.x[11] = 0xaabbccddeeff0011ULL;
    uint16_t instruction = (uint16_t)((7U << 13U) | (5U << 10U) | (2U << 7U) | (2U << 5U) | (3U << 2U));

    RiscVMMIOStore store = decode(instruction);

    TEST_ASSERT_EQUAL_UINT64(0x500000a8, store.address);
    TEST_ASSERT_EQUAL_UINT64(context.x[11], store.value);
    TEST_ASSERT_EQUAL_UINT64(8, store.width);
}

void test_mmio_store_performs_word_store_at_access_address_and_advances_pc(void)
{
    volatile uint32_t register_value = 0;
    context.sepc = 0x1000;
    RiscVMMIOStore store = RiscVMMIOStore__new(0x40003084, 0x1234567887654321ULL, sizeof(uint32_t),
                                               sizeof(uint16_t));

    RiscVMMIOStore__perform(&store, (Address)&register_value, &context);

    TEST_ASSERT_EQUAL_HEX32(0x87654321U, register_value);
    TEST_ASSERT_EQUAL_UINT64(0x1002, context.sepc);
}

void test_mmio_store_rejects_non_store_instruction(void)
{
    TEST_ASSERT_TRUE(is_nothing(RiscVMMIOStore__decode(0x00000013U, &context)));
}

void test_mmio_store_skips_exact_following_io_fence(void)
{
    context.sepc = 0x145be;

    TEST_ASSERT_TRUE(RiscVMMIOStore__skip_following_fence(0x0ff0000fU, &context));
    TEST_ASSERT_EQUAL_UINT64(0x145c2, context.sepc);
}

void test_mmio_store_keeps_other_following_instructions(void)
{
    context.sepc = 0x145be;

    TEST_ASSERT_FALSE(RiscVMMIOStore__skip_following_fence(0x0330000fU, &context));
    TEST_ASSERT_EQUAL_UINT64(0x145be, context.sepc);
    TEST_ASSERT_FALSE(RiscVMMIOStore__skip_following_fence(0x00000013U, &context));
    TEST_ASSERT_EQUAL_UINT64(0x145be, context.sepc);
}
