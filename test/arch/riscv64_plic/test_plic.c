#include "unity.h"
#include "../../../common/arch/riscv64/plic.h"
#include <stdlib.h>

static Address distributor;

static PlicPriorityRegisterPtr priority_register(IRQNumber irq)
{
    return (PlicPriorityRegisterPtr)(distributor + irq * sizeof(uint32_t));
}

static PlicPriorityRegisterPtr enable_register(void)
{
    return (PlicPriorityRegisterPtr)(distributor + 0x2080);
}

void setUp(void)
{
    distributor = (Address)calloc(1, 0x3000);
    TEST_ASSERT_NOT_EQUAL(0, distributor);
    *enable_register() = 0x5fc;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x5fc, *enable_register());
    free((void *)distributor);
}

void test_masking_and_rearming_use_per_source_priority_registers(void)
{
    Plic__set_masked(distributor, 8, false);
    TEST_ASSERT_EQUAL_UINT32(1, *priority_register(8));
    Plic__set_masked(distributor, 8, true);
    TEST_ASSERT_EQUAL_UINT32(0, *priority_register(8));
    Plic__set_masked(distributor, 8, false);
    TEST_ASSERT_EQUAL_UINT32(1, *priority_register(8));
}

void test_interleaved_mask_and_rearm_of_neighboring_irqs_do_not_lose_updates(void)
{
    Plic__set_masked(distributor, 8, true);
    Plic__set_masked(distributor, 10, true);
    Plic__set_masked(distributor, 8, false);
    TEST_ASSERT_EQUAL_UINT32(1, *priority_register(8));
    TEST_ASSERT_EQUAL_UINT32(0, *priority_register(10));
    Plic__set_masked(distributor, 10, false);
    TEST_ASSERT_EQUAL_UINT32(1, *priority_register(8));
    TEST_ASSERT_EQUAL_UINT32(1, *priority_register(10));
}

void test_irq_word_boundaries_do_not_change_context_enable_words(void)
{
    Plic__set_masked(distributor, 31, false);
    Plic__set_masked(distributor, 32, false);
    Plic__set_masked(distributor, 31, true);
    TEST_ASSERT_EQUAL_UINT32(0, *priority_register(31));
    TEST_ASSERT_EQUAL_UINT32(1, *priority_register(32));
}
