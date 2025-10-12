#include <stdint.h>

#include "unity.h"

#include "../../../kernel/bin/elf.h"
#include "../../../kernel/bin/embedded_binaries.h"
#include "../stubs/panic.h"

extern const unsigned char _bin_hello_world_start[];
extern const unsigned char _bin_hello_world_end[];

void setUp(void)
{
    clear_panic_state();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

static Elf * load_test_elf()
{
    return EmbeddedElf__load("hello_world");
}

static void check_section(
    SectionHeader * section,
    const char * name,
    SectionType type,
    Address virtual_address,
    Offset offset,
    Size size,
    EntrySize entry_size,
    SectionFlags flags,
    SectionInfo info,
    SectionIndex link,
    Alignment alignment)
{
    TEST_ASSERT_NOT_NULL(section);
    TEST_ASSERT_EQUAL_UINT(offset, section->offset);
    TEST_ASSERT_EQUAL_UINT(flags, section->flags);
    TEST_ASSERT_EQUAL_UINT(info, section->info);
    TEST_ASSERT_EQUAL_UINT(link, section->link);
    TEST_ASSERT_EQUAL_UINT(virtual_address, section->virtual_address);
    TEST_ASSERT_EQUAL_UINT(size, section->size);
    TEST_ASSERT_EQUAL_UINT(entry_size, section->entry_size);
    TEST_ASSERT_EQUAL_UINT(alignment, section->alignment);
    TEST_ASSERT_EQUAL_UINT(type, section->type);
}

void __test__sections(void)
{
    Elf * elf = load_test_elf();

    SectionHeaderArray sections = Elf__read_section_headers(elf);

    TEST_ASSERT_EQUAL_UINT(7, sections.len);
    SectionHeader * section_0 = SectionHeaderArray__at(&sections, 0);
    SectionHeader * section_1 = SectionHeaderArray__at(&sections, 1);
    SectionHeader * section_6 = SectionHeaderArray__at(&sections, 6);

    check_section(
        section_0, "", 0x0,
        0x00, 0x00,
        0x00, 0x00, 0,
        0, 0, 0x0);
    check_section(
        section_1, ".text", ProgramData,
        0x400000, 0x10000,
        0x58, 0x00, 0,
        0, 0, 0x0);
    check_section(
        section_6, ".shstrtab", StringTable,
        0x400000, 0x10323,
        0x32, 0x00, 0,
        0, 0, 0x01);


}

void test__map_binary_runnable(void)
{
    Elf * elf = load_test_elf();
    ExpandedSegmentArray addresses = Elf__map(elf);

    TEST_ASSERT_EQUAL_UINT(1, addresses.len);
    ExpandedSegment segment = ExpandedSegmentArray__at(&addresses, 0);
    TEST_ASSERT(segment.address.execute);
    TEST_ASSERT(segment.address.read);
    TEST_ASSERT_FALSE(segment.address.write);
    TEST_ASSERT_EQUAL_UINT(0x400000, segment.address.virtual_addr);
    TEST_ASSERT_EQUAL_UINT(101, segment.size);
}

void test__program_header(void)
{
    Elf * elf = load_test_elf();

    ProgramHeaderArray headers = Elf__read_program_headers(elf);

    TEST_ASSERT_EQUAL_UINT(1, headers.len);

    ProgramHeader * header = ProgramHeaderArray__at(&headers, 0);
    TEST_ASSERT_NOT_NULL(header);
    TEST_ASSERT_EQUAL_UINT(Load, header->type);
    TEST_ASSERT(header->flags & Read);
    TEST_ASSERT(header->flags & Execute);
    TEST_ASSERT_EQUAL_UINT(0x10000, header->segment_offset);
    TEST_ASSERT_EQUAL_UINT(0x400000, header->virtual_address);
    TEST_ASSERT_EQUAL_UINT(0x400000, header->physical_address);
    TEST_ASSERT_EQUAL_UINT(0x65, header->file_size);
    TEST_ASSERT_EQUAL_UINT(0x65, header->memory_size);
    TEST_ASSERT_EQUAL_UINT(0x10000, header->alignment);
}

void test_parse_elf_header(void)
{
    Elf * elf = load_test_elf();
    Elf64Header * header = Elf__read_header(elf);
    byte expected_magic[] = {0x7f, 0x45, 0x4c, 0x46};
    TEST_ASSERT_EQUAL_UINT(2, header->klass);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_magic, header->magic, 4);
    TEST_ASSERT_EQUAL_UINT(1, header->elf_version);
    TEST_ASSERT_EQUAL_UINT(1, header->ei_version);
    TEST_ASSERT_EQUAL_UINT(LittleEndian, header->endianness);
    TEST_ASSERT_EQUAL_UINT(0x00, header->target_OS);
    TEST_ASSERT_EQUAL_UINT(0xB7, header->machine);
    TEST_ASSERT_EQUAL_UINT(0x400000, header->entry);
    TEST_ASSERT_EQUAL_UINT(Executable, header->type);
    TEST_ASSERT_EQUAL_UINT(0, header->abi_version);
    TEST_ASSERT_EQUAL_UINT(64, header->program_header_offset);
    TEST_ASSERT_EQUAL_UINT(66392, header->section_header_offset);
    TEST_ASSERT_EQUAL_UINT(0x00000000, header->flags);
    TEST_ASSERT_EQUAL_UINT(64, header->header_size);
    TEST_ASSERT_EQUAL_UINT(1, header->program_header_entries);
    TEST_ASSERT_EQUAL_UINT(64, header->section_header_size);
    TEST_ASSERT_EQUAL_UINT(7, header->section_header_entries);
    TEST_ASSERT_EQUAL_UINT(6, header->section_name_index);
}
