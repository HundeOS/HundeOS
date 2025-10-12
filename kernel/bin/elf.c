#include "elf.h"

#include "../debug.h"
#include "../panic.h"
#include "../kalloc.h"
#include "../print.h"
#include "../types/fixed_array.h"

FixedArrayBodyDefine(SectionHeaderArray);
FixedArrayBodyDefine(ProgramHeaderArray);
FixedArrayBodyDefine(ExpandedSegmentArray)

Elf64Header * Elf__read_header(const Elf * self)
{
    return ptr(self->start, Elf64Header);
}

SectionHeaderArray Elf__read_section_headers(const Elf * self)
{
    SectionHeaderArray headers = SectionHeaderArray__new();

    Elf64Header * header = Elf__read_header(self);
    Size section_header_size = header->section_header_size;
    Size section_header_entries = header->section_header_entries;
    Offset section_header_offset = header->section_header_offset;
    Offset section_names = header->section_name_index;

    SectionHeader * start = offset_ptr(self->start, section_header_offset, SectionHeader);
    k_printf("header offset=%p size=%i names=%p\n", section_header_offset, section_header_size, section_names);
    for (int i = 0; i < section_header_entries; i++)
    {
        //0x10358:
        SectionHeader * header = offset_ptr(start, i * section_header_size, SectionHeader);
        k_printf("header=%i %p %p \n", i, header, header->name_offset);
        SectionHeaderArray__push(&headers, header);
    }
    return headers;
}

ProgramHeaderArray Elf__read_program_headers(const Elf * self)
{
    Elf64Header * header = Elf__read_header(self);
    __d_println(FOCUS, "ELF headers read");
    Offset program_header_offset = header->program_header_offset;
    __d_println(FOCUS, "ELF headers read - PH offset");
    uint64_t num_headers = header->program_header_entries;
    Size program_header_size = header->program_header_entry_size;
    __d_println(FOCUS, "Prepare to read program headers");

    ProgramHeaderArray headers = ProgramHeaderArray__new();
    __d_println(FOCUS, "Array init");
    Offset start = self->start + program_header_offset;
    for (int i = 0; i < num_headers; i++)
    {
        ProgramHeaderArray__push(&headers, offset_ptr(start, i * program_header_size, ProgramHeader));
    }
    return headers;
}

static UserAddress Elf__explode(const Elf * self, ProgramHeader * header)
{
    __d_printf(FOCUS, "Elf__explode %p", header->memory_size);
    UserAddress address = Kernel__alloc_user(
        header->virtual_address,
        header->memory_size,
        header->flags & Read,
        header->flags & Write,
        header->flags & Execute);

    __d_println(FOCUS, "Elf__explode");
    Kernel__memcopy(offset_addr(self->start, header->segment_offset),
        address.kernel_addr.virtual_addr,
        header->file_size);
    __d_println(FOCUS, "Elf__explode - done");
    return address;
}

ExpandedSegmentArray Elf__map(const Elf * self) {
    __d_printf(FOCUS, "Reading elf %p\n", self);
    ProgramHeaderArray headers = Elf__read_program_headers(self);
    __d_println(FOCUS, "Headers loaded");
    ExpandedSegmentArray expanded_segments = ExpandedSegmentArray__new();

    __d_println(FOCUS, "Expanded segments");
    for (int i = 0; i < headers.len; i++)
    {
        __d_println(FOCUS, "Elf__map");
        ProgramHeader * header = ProgramHeaderArray__at(&headers, i);
        if (header->type == Load)
        {
            UserAddress address = Elf__explode(self, header);

            ExpandedSegment segment = (ExpandedSegment) {
                .address = address,
                .size = header->memory_size,
            };

            ExpandedSegmentArray__push(&expanded_segments, segment);
        }
    }
    __d_println(FOCUS, "Elf__done");
    return expanded_segments;
}
