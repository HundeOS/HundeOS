#include "elf.h"

#include "../debug.h"
#include "../panic.h"
#include "../kalloc.h"
#include "../print.h"
#include "../arch/cache.h"
#include "../all_defs.h"
#include "../types/fixed_array.h"

FixedArrayBodyDefine(SectionHeaderArray);
FixedArrayBodyDefine(ProgramHeaderArray);
FixedArrayBodyDefine(ExpandedSegmentArray);

static Address Elf__page_start(Address address)
{
    return address & ~(PAGE_SIZE - 1ULL);
}

static size_t Elf__page_allocation_size(Address address, size_t size)
{
    Offset page_offset = address - Elf__page_start(address);
    return (page_offset + size + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
}

Elf Elf__new(const Address start, const Address end, const BString name)
{
    return (Elf) {
        .start = start,
        .end = end,
        .name = name,
    };
}

Elf64Header * Elf__read_header(Elf self)
{
    return ptr(self.start, Elf64Header);
}

SectionHeaderArray Elf__read_section_headers(Elf self)
{
    SectionHeaderArray headers = SectionHeaderArray__new();

    Elf64Header * header = Elf__read_header(self);
    Size section_header_size = header->section_header_size;
    Size section_header_entries = header->section_header_entries;
    Offset section_header_offset = header->section_header_offset;
    Offset section_names = header->section_name_index;

    SectionHeaderPtr start = offset_ptr(self.start, section_header_offset, SectionHeader);
    __d_printf(PROCESS, "header offset=%p size=%i names=%p\n", section_header_offset, section_header_size, section_names);
    for (int i = 0; i < section_header_entries; i++)
    {
        SectionHeaderPtr header = offset_ptr(start, i * section_header_size, SectionHeader);

        ResultMatch(CollectionCode, SectionHeaderArray__push(&headers, header) );
            IsError(code, message, __d_println(PANIC, cstr(message));)
        End

    }
    return headers;
}

ProgramHeaderArray Elf__read_program_headers(Elf self)
{
    Elf64Header * header = Elf__read_header(self);
    Offset program_header_offset = header->program_header_offset;
    uint64_t num_headers = header->program_header_entries;
    Size program_header_size = header->program_header_entry_size;

    ProgramHeaderArray headers = ProgramHeaderArray__new();
    Offset start = self.start + program_header_offset;
    for (int i = 0; i < num_headers; i++)
    {
        ProgramHeaderPtr header = offset_ptr(start, i * program_header_size, ProgramHeader);

        ResultMatch(CollectionCode, ProgramHeaderArray__push(&headers, header));
            IsError(code, message, __d_println(PANIC, cstr(message));)
        End
    }
    return headers;
}

static UserAddress Elf__explode(const Elf self, ProgramHeaderPtr header)
{
    Address segment_start = header->virtual_address;
    Address page_start = Elf__page_start(segment_start);
    Offset page_offset = segment_start - page_start;
    size_t allocation_size = Elf__page_allocation_size(segment_start, header->memory_size);

    UserAddress page_address = Kernel__alloc_user(
        page_start,
        allocation_size,
        header->flags & Read,
        header->flags & Write,
        header->flags & Execute);
    UserAddress address = UserAddress__relative(&page_address, page_offset);

    Kernel__memset(page_address.kernel_addr.virtual_addr, 0, allocation_size);
    Kernel__memcopy(offset_addr(self.start, header->segment_offset),
        address.kernel_addr.virtual_addr,
        header->file_size);
    if (header->flags & Execute)
    {
        Arch__sync_instruction_memory(address.kernel_addr.virtual_addr, header->file_size);
    }
    return page_address;
}

static void Elf__expand_segment(
    Elf self, ProgramHeaderPtr header, ExpandedSegmentArrayPtr expanded_segments
)
{
    __d_printf(PROCESS, "Expanding segment header=%p name=%s\n", header, cstr(self.name));
    if (header->type == Load)
    {
        UserAddress address = Elf__explode(self, header);
        ExpandedSegment segment = (ExpandedSegment) {
            .address = address,
            .size = Elf__page_allocation_size(header->virtual_address, header->memory_size),
        };
        ExpandedSegmentArray__push(expanded_segments, segment);
    }

}

ExpandedSegmentArray Elf__map(const Elf self) {
    ProgramHeaderArray headers = Elf__read_program_headers(self);
    ExpandedSegmentArray expanded_segments = ExpandedSegmentArray__new();
    __d_printf(DEBUG, "Mapping segments %i\n", ProgramHeaderArray__len(&headers));

    for (int i = 0; i < headers.len; i++)
    {
        Match(ProgramHeaderPtr, ProgramHeaderArray__at(&headers, i));
            IsNone(__d_println(PANIC, "No header");)
            IsSome(header, Elf__expand_segment(self, header, &expanded_segments);)
        End
    }
    return expanded_segments;
}

Optional(ExpandedSegment) Elf__entry_segment(Elf self, ExpandedSegmentArrayPtr expanded_segments)
{
    Address entry = Elf__read_header(self)->entry;
    ExpandedSegmentArray segments = *expanded_segments;

    FAIterate(ExpandedSegmentArray, segments, i, segment)
        Address start = segment.address.virtual_addr;
        Address end = start + segment.size;
        if (entry >= start && entry < end)
        {
            return Some(ExpandedSegment, segment);
        }
    End

    return None(ExpandedSegment);
}
