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

void test_elf_list_uses_embedded_index(void)
{
    BStringArray files = EmbeddedElf__list();

    TEST_ASSERT_EQUAL_UINT(9, files.len);
    TEST_ASSERT_TRUE(BString__equals(&files.data[0], &bstr("hello_world")));
    TEST_ASSERT_TRUE(BString__equals(&files.data[1], &bstr("services/idle")));
    TEST_ASSERT_TRUE(BString__equals(&files.data[2], &bstr("shell")));
    TEST_ASSERT_TRUE(BString__equals(&files.data[3], &bstr("services/hundeschule")));
    TEST_ASSERT_TRUE(BString__equals(&files.data[4], &bstr("services/hundeschau")));
    TEST_ASSERT_TRUE(BString__equals(&files.data[5], &bstr("services/humpen")));
    TEST_ASSERT_TRUE(BString__equals(&files.data[6], &bstr("drivers/virtio_blk")));
}

void test_elf_lookup_missing_file(void)
{
    Optional(Elf) elf = EmbeddedElf__load(&bstr("missing"));

    match_optional(elf,
        some_test_fail(),
        none_do_nothing()
    );
}
