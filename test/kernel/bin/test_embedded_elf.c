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
    Optional(Elf) elf = EmbeddedElf__load(bstring("hello_world"));

    match_optional(Elf, elf, some_do_nothing(), none_test_fail());
}

void test_elf_list_uses_embedded_index(void)
{
    BString hello_world = bstr("hello_world");
    BString idle = bstr("idle");
    BString shell = bstr("shell");
    BString hundeschule = bstr("hundeschule");
    BString hundeschau = bstr("hundeschau");
    BString humpen = bstr("services/humpen");
    BString virtio_blk = bstr("drivers/virtio_blk");
    BStringArray files = EmbeddedElf__list();

    TEST_ASSERT_EQUAL_UINT(9, files.len);
    TEST_ASSERT_TRUE(BString__equals(&files.data[0], &hello_world));
    TEST_ASSERT_TRUE(BString__equals(&files.data[1], &idle));
    TEST_ASSERT_TRUE(BString__equals(&files.data[2], &shell));
    TEST_ASSERT_TRUE(BString__equals(&files.data[3], &hundeschule));
    TEST_ASSERT_TRUE(BString__equals(&files.data[4], &hundeschau));
    TEST_ASSERT_TRUE(BString__equals(&files.data[5], &humpen));
    TEST_ASSERT_TRUE(BString__equals(&files.data[6], &virtio_blk));
}

void test_elf_lookup_missing_file(void)
{
    Optional(Elf) elf = EmbeddedElf__load(bstring("missing"));

    match_optional(Elf, elf, some_test_fail(), none_do_nothing());
}
