#include "unity.h"

#include "../../../user/lib/wuff/arch.h"

void test_riscv64_wuff_relocation_types(void)
{
    TEST_ASSERT_EQUAL(243, WuffArch__machine());
    TEST_ASSERT_EQUAL(WUFF_RELOCATION_ABSOLUTE, WuffArch__relocation_kind(2));
    TEST_ASSERT_EQUAL(WUFF_RELOCATION_RELATIVE, WuffArch__relocation_kind(3));
    TEST_ASSERT_EQUAL(WUFF_RELOCATION_JUMP_SLOT, WuffArch__relocation_kind(5));
    TEST_ASSERT_EQUAL(WUFF_RELOCATION_UNSUPPORTED, WuffArch__relocation_kind(0xffff));
}
