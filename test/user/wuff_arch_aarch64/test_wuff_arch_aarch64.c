#include "unity.h"

#include "../../../user/lib/wuff/arch.h"

void test_aarch64_wuff_relocation_types(void)
{
    TEST_ASSERT_EQUAL(183, WuffArch__machine());
    TEST_ASSERT_EQUAL(WUFF_RELOCATION_ABSOLUTE, WuffArch__relocation_kind(257));
    TEST_ASSERT_EQUAL(WUFF_RELOCATION_GLOBAL_DATA, WuffArch__relocation_kind(1025));
    TEST_ASSERT_EQUAL(WUFF_RELOCATION_JUMP_SLOT, WuffArch__relocation_kind(1026));
    TEST_ASSERT_EQUAL(WUFF_RELOCATION_RELATIVE, WuffArch__relocation_kind(1027));
    TEST_ASSERT_EQUAL(WUFF_RELOCATION_UNSUPPORTED, WuffArch__relocation_kind(0xffff));
}
