#include "unity.h"

#include "../../../kernel/bin/embedded_binaries.h"
#include "../../../kernel/types/kstring_additional_types.h"

extern const unsigned char _bin_hello_world_start[];
extern const unsigned char _bin_hello_world_end[];

void setUp(void)
{

}

void test_elf_lookup(void)
{
    Optional(Elf) elf = EmbeddedElf__load(kstring("hello_world"));

    Match(Elf, elf)
        IsNone(TEST_FAIL())
    End
}

void test_elf_list_uses_embedded_index(void)
{
    KString hello_world = KSTRING_NEW("hello_world");
    KString idle = KSTRING_NEW("idle");
    KString shell = KSTRING_NEW("shell");
    KStringArray files = EmbeddedElf__list();

    TEST_ASSERT_EQUAL_UINT(3, files.len);
    TEST_ASSERT_TRUE(KString__equals(&files.data[0], &hello_world));
    TEST_ASSERT_TRUE(KString__equals(&files.data[1], &idle));
    TEST_ASSERT_TRUE(KString__equals(&files.data[2], &shell));
}

void test_elf_lookup_missing_file(void)
{
    Optional(Elf) elf = EmbeddedElf__load(kstring("missing"));

    Match(Elf, elf)
        IsSome(_,
        {
            TEST_FAIL();
        })
    End
}
