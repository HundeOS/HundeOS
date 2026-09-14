#include "unity.h"

#include "../../../kernel/bin/embedded_binaries.h"
#include "../../../kernel/types/bstring_additional_types.h"

extern const unsigned char _bin_hello_world_start[];
extern const unsigned char _bin_hello_world_end[];

void setUp(void)
{
}

void test_elf_lookup(void)
{
    Optional(Elf) elf = EmbeddedElf__load(&bstr("hello_world"));

    match_optional(elf,
        some_do_nothing(),
        none_test_fail()
    );
}

static void assert_embedded_name(BStringArray * files, ArrayIndex index, ConstBStringPtr expected)
{
    match_optional(
        BStringArray__at(files, index),
        some(name) { TEST_ASSERT_EQUAL_BSTRING(name, *expected); },
        none_test_fail()
    );
}

void test_elf_list_uses_embedded_index(void)
{
    BStringArray files = EmbeddedElf__list();

    TEST_ASSERT_EQUAL_UINT(9, BStringArray__len(&files));
    assert_embedded_name(&files, 0, &bstr("hello_world"));
    assert_embedded_name(&files, 1, &bstr("services/idle"));
    assert_embedded_name(&files, 2, &bstr("shell"));
    assert_embedded_name(&files, 3, &bstr("services/hundeschule"));
    assert_embedded_name(&files, 4, &bstr("services/hundeschau"));
    assert_embedded_name(&files, 5, &bstr("services/humpen"));
    assert_embedded_name(&files, 6, &bstr("drivers/virtio_blk_channelio"));
}

void test_elf_lookup_missing_file(void)
{
    Optional(Elf) elf = EmbeddedElf__load(&bstr("missing"));

    match_optional(elf,
        some_test_fail(),
        none_do_nothing()
    );
}
