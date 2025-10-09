#include "unity.h"

#include "../../../kernel/bin/elf.h"
#include "../../../kernel/bin/embedded_binaries.h"

extern const unsigned char _bin_hello_world_start[];
extern const unsigned char _bin_hello_world_end[];

void setUp(void)
{

}

void test_parse_elf_header(void)
{
    EmbeddedElf * elf = EmbeddedElf__load("hello_world");
    Elf64Header * header = EmbeddedElf__read_header(elf);
    byte expected_magic[] = {0x7f, 0x45, 0x4c, 0x46};
    TEST_ASSERT_EQUAL_UINT8(2, header->klass);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_magic, header->magic, 4);
    TEST_ASSERT_EQUAL_UINT32(1, header->elf_version);
    TEST_ASSERT_EQUAL_UINT8(1, header->ei_version);
    TEST_ASSERT_EQUAL_UINT8(LittleEndian, header->endianness);
    TEST_ASSERT_EQUAL_UINT8(0x00, header->target_OS);
    TEST_ASSERT_EQUAL_UINT8(0xB7, header->machine);
    TEST_ASSERT_EQUAL_UINT64(0x400000, header->entry);
    TEST_ASSERT_EQUAL_UINT16(Executable, header->type);
    TEST_ASSERT_EQUAL_UINT8(0, header->abi_version);
    TEST_ASSERT_EQUAL_UINT64(64, header->program_header_offset);
    TEST_ASSERT_EQUAL_UINT64(66392, header->section_header_offset);
    TEST_ASSERT_EQUAL_UINT32(0x00000000, header->flags);
    TEST_ASSERT_EQUAL_UINT16(64, header->header_size);
    TEST_ASSERT_EQUAL_UINT16(1, header->program_header_entries);
    TEST_ASSERT_EQUAL_UINT16(64, header->section_header_size);
    TEST_ASSERT_EQUAL_UINT16(7, header->section_header_entries);
    TEST_ASSERT_EQUAL_UINT16(6, header->section_name_index);
}

void tearDown(void)
{

}