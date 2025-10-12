#include "unity.h"

#include "../../../kernel/bin/embedded_binaries.h"

extern const unsigned char _bin_hello_world_start[];
extern const unsigned char _bin_hello_world_end[];

void setUp(void)
{

}

void test_elf_lookup(void)
{
    Elf * elf = EmbeddedElf__load("hello_world");

    TEST_ASSERT_NOT_NULL(elf);
}