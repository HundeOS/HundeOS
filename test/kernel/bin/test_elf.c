#include <stdint.h>

#include "unity.h"

#include "../../../kernel/types/optional.h"
#include "../../../kernel/bin/elf.h"
#include "../../../kernel/bin/embedded_binaries.h"
#include "../stubs/panic.h"
#include "../stubs/kalloc.h"

extern const unsigned char _bin_hello_world_start[];
extern const unsigned char _bin_hello_world_end[];

void setUp(void)
{
    clear_panic_state();
    KAllocStub__set_alloc_fill(false, 0);
}

void tearDown(void)
{
    KAllocStub__set_alloc_fill(false, 0);
    TEST_ASSERT_FALSE(panic_occurred());
}

static Elf load_test_elf()
{
    return unwrap_optional(EmbeddedElf__load(&bstr("hello_world")), (Elf){});
}

static void check_section(Optional(SectionHeaderPtr) opt_section, const char * name, SectionType type,
                          Address virtual_address, Offset offset, Size size, EntrySize entry_size, SectionFlags flags,
                          SectionInfo info, SectionIndex link, Alignment alignment)
{
    match_optional(
        opt_section,
        some(section)
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
        },
        none_test_fail()
    );
}

void __test__sections(void)
{
    Elf elf = load_test_elf();

    SectionHeaderArray sections = Elf__read_section_headers(&elf);

    TEST_ASSERT_EQUAL_UINT(7, sections.len);
    Optional(SectionHeaderPtr) section_0 = SectionHeaderArray__at(&sections, 0);
    Optional(SectionHeaderPtr) section_1 = SectionHeaderArray__at(&sections, 1);
    Optional(SectionHeaderPtr) section_6 = SectionHeaderArray__at(&sections, 6);

    check_section(section_0, "", 0x0, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0x0);
    check_section(section_1, ".text", ProgramData, 0x400000, 0x10000, 0x58, 0x00, 0, 0, 0, 0x0);
    check_section(section_6, ".shstrtab", StringTable, 0x400000, 0x10323, 0x32, 0x00, 0, 0, 0, 0x01);
}

void test_map_binary_runnable(void)
{
    Elf elf = load_test_elf();
    ExpandedSegmentArray addresses = Elf__map(&elf);

    TEST_ASSERT_EQUAL_UINT(1, addresses.len);
    Optional(ExpandedSegment) segment_opt = ExpandedSegmentArray__at(&addresses, 0);
    match_optional(
        segment_opt,
        some(segment)
        {
            TEST_ASSERT(segment.address.execute);
            TEST_ASSERT(segment.address.read);
            TEST_ASSERT_FALSE(segment.address.write);
            TEST_ASSERT_EQUAL_UINT(0x400000, segment.address.virtual_addr);
            TEST_ASSERT_EQUAL_UINT(4096, segment.size);
        },
        none_test_fail()
    );
}

void test_map_zeroes_load_segment_memory_beyond_file_size(void)
{
    uint8_t image[256] = {0};
    Elf64HeaderPtr header = ptr(image, Elf64Header);
    ProgramHeaderPtr program_header = ptr(&image[64], ProgramHeader);
    uint8_t * file_data = &image[128];

    file_data[0] = 0x11;
    file_data[1] = 0x22;
    file_data[2] = 0x33;
    file_data[3] = 0x44;

    header->program_header_offset = 64;
    header->program_header_entry_size = sizeof(ProgramHeader);
    header->program_header_entries = 1;

    *program_header = (ProgramHeader){
        .type = Load,
        .flags = Read | Write,
        .segment_offset = 128,
        .virtual_address = 0x500000,
        .physical_address = 0x500000,
        .file_size = 4,
        .memory_size = 16,
        .alignment = 0x1000,
    };

    KAllocStub__set_alloc_fill(true, 0xaa);
    Elf elf = Elf__new((Address)image, (Address)&image[sizeof(image)], &bstr("bss_test"));
    ExpandedSegmentArray segments = Elf__map(&elf);
    KAllocStub__set_alloc_fill(false, 0);

    TEST_ASSERT_EQUAL_UINT(1, ExpandedSegmentArray__len(&segments));
    match_optional(
        ExpandedSegmentArray__at(&segments, 0),
        some(segment)
        {
            uint8_t * mapped = ptr(segment.address.kernel_addr.virtual_addr, uint8_t);
            TEST_ASSERT_EQUAL_UINT8(0x11, mapped[0]);
            TEST_ASSERT_EQUAL_UINT8(0x22, mapped[1]);
            TEST_ASSERT_EQUAL_UINT8(0x33, mapped[2]);
            TEST_ASSERT_EQUAL_UINT8(0x44, mapped[3]);
            for (size_t index = 4; index < 16; index++)
            {
                TEST_ASSERT_EQUAL_UINT8(0, mapped[index]);
            }
        },
        none_test_fail()
    );
}

void test_map_handles_unaligned_load_segment_memory(void)
{
    uint8_t image[256] = {0};
    Elf64HeaderPtr header = ptr(image, Elf64Header);
    ProgramHeaderPtr program_header = ptr(&image[64], ProgramHeader);
    uint8_t * file_data = &image[128];

    file_data[0] = 0x99;
    file_data[1] = 0x88;
    file_data[2] = 0x77;
    file_data[3] = 0x66;

    header->program_header_offset = 64;
    header->program_header_entry_size = sizeof(ProgramHeader);
    header->program_header_entries = 1;

    *program_header = (ProgramHeader){
        .type = Load,
        .flags = Read | Write,
        .segment_offset = 128,
        .virtual_address = 0x5002d8,
        .physical_address = 0x5002d8,
        .file_size = 4,
        .memory_size = 16,
        .alignment = 0x1000,
    };

    KAllocStub__set_alloc_fill(true, 0xaa);
    Elf elf = Elf__new((Address)image, (Address)&image[sizeof(image)], &bstr("unaligned_bss_test"));
    ExpandedSegmentArray segments = Elf__map(&elf);
    KAllocStub__set_alloc_fill(false, 0);

    TEST_ASSERT_EQUAL_UINT(1, ExpandedSegmentArray__len(&segments));
    match_optional(
        ExpandedSegmentArray__at(&segments, 0),
        some(segment)
        {
            TEST_ASSERT_EQUAL_UINT(0x500000, segment.address.virtual_addr);
            TEST_ASSERT_EQUAL_UINT(4096, segment.size);

            uint8_t * page = ptr(segment.address.kernel_addr.virtual_addr, uint8_t);
            uint8_t * mapped = page + 0x2d8;

            TEST_ASSERT_EQUAL_UINT8(0, page[0]);
            TEST_ASSERT_EQUAL_UINT8(0, page[0x2d7]);
            TEST_ASSERT_EQUAL_UINT8(0x99, mapped[0]);
            TEST_ASSERT_EQUAL_UINT8(0x88, mapped[1]);
            TEST_ASSERT_EQUAL_UINT8(0x77, mapped[2]);
            TEST_ASSERT_EQUAL_UINT8(0x66, mapped[3]);
            for (size_t index = 4; index < 16; index++)
            {
                TEST_ASSERT_EQUAL_UINT8(0, mapped[index]);
            }
        },
        none_test_fail()
    );
}

void test_program_header(void)
{
    Elf elf = load_test_elf();

    ProgramHeaderArray headers = Elf__read_program_headers(&elf);

    TEST_ASSERT_EQUAL_UINT(1, headers.len);

    Optional(ProgramHeaderPtr) opt_header = ProgramHeaderArray__at(&headers, 0);

    match_optional(
        opt_header,
        some(header)
        {
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
        },
        none_test_fail()
    );
}

void test_parse_elf_header(void)
{
    Elf elf = load_test_elf();
    ConstElf64HeaderPtr header = Elf__read_header(&elf);
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
