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
        IntResult res = SectionHeaderArray__push(&headers, header);
    }
    return headers;
}

ProgramHeaderArray Elf__read_program_headers(const Elf * self)
{
    Elf64Header * header = Elf__read_header(self);
    Offset program_header_offset = header->program_header_offset;
    uint64_t num_headers = header->program_header_entries;
    Size program_header_size = header->program_header_entry_size;

    ProgramHeaderArray headers = ProgramHeaderArray__new();
    Offset start = self->start + program_header_offset;
    for (int i = 0; i < num_headers; i++)
    {
        ProgramHeader * header = offset_ptr(start, i * program_header_size, ProgramHeader);

        ResultMatch(IntResult, ProgramHeaderArray__push(&headers, header) );
            IsError(code, message, __d_println(PANIC, message.data);)
        End
    }
    return headers;
}

static UserAddress Elf__explode(const Elf * self, ProgramHeader * header)
{
    UserAddress address = Kernel__alloc_user(
        header->virtual_address,
        header->memory_size,
        header->flags & Read,
        header->flags & Write,
        header->flags & Execute);

    Kernel__memcopy(offset_addr(self->start, header->segment_offset),
        address.kernel_addr.virtual_addr,
        header->file_size);
    return address;
}

static ExpandedSegment Elf__expand_segment(Elf * self,
                                           ProgramHeader * header,
                                           ExpandedSegmentArray * expanded_segments)
{
    __d_println(FOCUS, "Expanding segment");
    if (header->type == Load)
    {
        UserAddress address = Elf__explode(self, header);
        ExpandedSegment segment = (ExpandedSegment) {
            .address = address,
            .size = header->memory_size,
        };
        ExpandedSegmentArray__push(expanded_segments, segment);
    }

}

ExpandedSegmentArray Elf__map(const Elf * self) {
    ProgramHeaderArray headers = Elf__read_program_headers(self);
    ExpandedSegmentArray expanded_segments = ExpandedSegmentArray__new();
    __d_printf(FOCUS, "Mapping segments %i\n", headers.len);

    for (int i = 0; i < headers.len; i++)
    {
        Match(ProgramHeaderPtr, ProgramHeaderArray__at(&headers, i))
            IsNone(__d_println(PANIC, "No header");)
            IsSome(header, Elf__expand_segment(self, header, &expanded_segments);)
        End
    }
    return expanded_segments;
}
