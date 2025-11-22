#include "unity.h"

#include "../../../kernel/bin/embedded_binaries.h"

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