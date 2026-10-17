#include "elf.h"

#include "../debug.h"
#include "../panic.h"
#include "../kalloc.h"
#include "../print.h"
#include "../arch/cache.h"
#include "../all_defs.h"
#include "../types/fixed_array.h"
#include "../arch/process.h"

FixedArrayBodyDefine(SectionHeaderArray);
FixedArrayBodyDefine(ProgramHeaderArray);
FixedArrayBodyDefine(ExpandedSegmentArray);

static bool Elf__valid_header(ConstElfPtr self)
{
    if (self->end < self->start || self->end - self->start < sizeof(Elf64Header))
    {
        return false;
    }
    uint64_t size = self->end - self->start;
    ConstElf64HeaderPtr header = Elf__read_header(self);
    if (header->magic[0] != 0x7f || header->magic[1] != 'E' || header->magic[2] != 'L' || header->magic[3] != 'F' ||
        header->klass != 2 || header->endianness != LittleEndian || header->ei_version != 1 ||
        header->elf_version != 1 || header->machine != Arch__elf_machine() ||
        header->header_size != sizeof(Elf64Header) || header->program_header_entry_size != sizeof(ProgramHeader) ||
        header->program_header_entries == 0 || header->program_header_entries > ProgramHeaderArray__max_size ||
        header->program_header_offset % _Alignof(ProgramHeader) != 0 || header->program_header_offset > size ||
        header->program_header_entries > (size - header->program_header_offset) / sizeof(ProgramHeader))
    {
        return false;
    }
    return true;
}

Result(ElfImageKind) Elf__image_kind(ConstElfPtr self)
{
    BString invalid = bstr("Invalid executable image");
    if (!Elf__valid_header(self))
    {
        return Error(ElfImageKind, invalid, -1);
    }
    ConstElf64HeaderPtr header = Elf__read_header(self);
    if (header->type == Executable)
    {
        match_result(
            Elf__validate(self),
            success_do_nothing(),
            error(code, message)
            {
                return Error(ElfImageKind, message, code);
            }
        );
        return Success(ElfImageKind, ELF_IMAGE_STATIC);
    }
    if (header->type != Shared)
    {
        return Error(ElfImageKind, invalid, -1);
    }
    uint64_t size = self->end - self->start;
    unsigned interpreter_count = 0;
    unsigned dynamic_count = 0;
    ProgramHeaderArray headers = Elf__read_program_headers(self);
    for_each_fa(
        ProgramHeaderArray,
        headers,
        index,
        segment,
        {
            if (segment->type == Dynamic)
            {
                dynamic_count++;
            }
            if (segment->type != Interpreter)
            {
                continue;
            }
            interpreter_count++;
            if (segment->segment_offset > size || segment->file_size > size - segment->segment_offset ||
                segment->file_size != sizeof(WUFF_INTERPRETER_PATH) || segment->memory_size < segment->file_size)
            {
                return Error(ElfImageKind, invalid, -1);
            }
            const char * path_chars = ptr(self->start + segment->segment_offset, char);
            BString path = BString__from_chars(path_chars, segment->file_size - 1);
            if (path_chars[segment->file_size - 1] != '\0' || !BString__equals(&path, &bstr(WUFF_INTERPRETER_PATH)))
            {
                return Error(ElfImageKind, invalid, -1);
            }
        }
    );
    if (interpreter_count != 1 || dynamic_count == 0)
    {
        return Error(ElfImageKind, invalid, -1);
    }
    return Success(ElfImageKind, ELF_IMAGE_DYNAMIC);
}

Result(Elf) Elf__validate(ConstElfPtr self)
{
    BString invalid = bstr("Invalid executable image");
    if (!Elf__valid_header(self) || Elf__read_header(self)->type != Executable)
    {
        return Error(Elf, invalid, -1);
    }
    uint64_t size = self->end - self->start;
    ConstElf64HeaderPtr header = Elf__read_header(self);
    ProgramHeaderArray headers = Elf__read_program_headers(self);
    bool entry_found = false;
    uint64_t memory = 0;
    for_each_fa(
        ProgramHeaderArray,
        headers,
        index,
        segment,
        {
            if (segment->type == Interpreter || segment->type == Dynamic)
            {
                return Error(Elf, invalid, -1);
            }
            if (segment->type != Load)
            {
                continue;
            }
            if (segment->memory_size == 0 || segment->file_size > segment->memory_size ||
                segment->segment_offset > size || segment->file_size > size - segment->segment_offset ||
                segment->virtual_address < PAGE_SIZE || segment->virtual_address >= BASE_STACK ||
                segment->memory_size > BASE_STACK - segment->virtual_address ||
                segment->flags & ~(Read | Write | Execute) ||
                (segment->alignment > 1 && ((segment->alignment & (segment->alignment - 1)) != 0 ||
                                            segment->virtual_address % segment->alignment !=
                                                segment->segment_offset % segment->alignment)))
            {
                return Error(Elf, invalid, -1);
            }
            memory += segment->memory_size;
            if (memory > 128ULL * 1024 * 1024)
            {
                return Error(Elf, invalid, -1);
            }
            Address start = segment->virtual_address & ~(PAGE_SIZE - 1ULL);
            Address end =
                (segment->virtual_address + segment->memory_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1ULL);
            for_each_fa(
                ProgramHeaderArray,
                headers,
                other_index,
                other,
                {
                    if (other_index >= index || other->type != Load)
                    {
                        continue;
                    }
                    Address other_start = other->virtual_address & ~(PAGE_SIZE - 1ULL);
                    Address other_end = (other->virtual_address + other->memory_size + PAGE_SIZE - 1) &
                                        ~(PAGE_SIZE - 1ULL);
                    if (start < other_end && other_start < end)
                    {
                        return Error(Elf, invalid, -1);
                    }
                }
            );
            if ((segment->flags & Execute) && header->entry >= segment->virtual_address &&
                header->entry - segment->virtual_address < segment->file_size)
            {
                entry_found = true;
            }
        }
    );
    return entry_found ? Success(Elf, *self) : Error(Elf, invalid, -1);
}

static Address Elf__page_start(Address address)
{
    return address & ~(PAGE_SIZE - 1ULL);
}

static size_t Elf__page_allocation_size(Address address, size_t size)
{
    Offset page_offset = address - Elf__page_start(address);
    return (page_offset + size + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
}

Elf Elf__new(const Address start, const Address end, ConstBStringPtr name)
{
    return (Elf){
        .start = start,
        .end = end,
        .name = *name,
    };
}

ExpandedSegment ExpandedSegment__new(UserAddress address, size_t size)
{
    return (ExpandedSegment){
        .address = address,
        .size = size,
    };
}

ConstElf64HeaderPtr Elf__read_header(ConstElfPtr self)
{
    return ptr(self->start, Elf64Header);
}

SectionHeaderArray Elf__read_section_headers(ConstElfPtr self)
{
    SectionHeaderArray headers = SectionHeaderArray__new();

    ConstElf64HeaderPtr header = Elf__read_header(self);
    Size section_header_size = header->section_header_size;
    Size section_header_entries = header->section_header_entries;
    Offset section_header_offset = header->section_header_offset;
    Offset section_names = header->section_name_index;

    SectionHeaderPtr start = offset_ptr(self->start, section_header_offset, SectionHeader);
    __d_printf(PROCESS, "header offset=%p size=%i names=%p\n", section_header_offset, section_header_size,
               section_names);
    for (int i = 0; i < section_header_entries; i++)
    {
        SectionHeaderPtr header = offset_ptr(start, i * section_header_size, SectionHeader);

        match_result(
            SectionHeaderArray__push(&headers, header),
            success_do_nothing(),
            error(code, message)
            {
                __d_println(PANIC, cstr(message));
                ;
            }
        );
    }
    return headers;
}

ProgramHeaderArray Elf__read_program_headers(ConstElfPtr self)
{
    ConstElf64HeaderPtr header = Elf__read_header(self);
    Offset program_header_offset = header->program_header_offset;
    uint64_t num_headers = header->program_header_entries;
    Size program_header_size = header->program_header_entry_size;

    ProgramHeaderArray headers = ProgramHeaderArray__new();
    Offset start = self->start + program_header_offset;
    for (int i = 0; i < num_headers; i++)
    {
        ProgramHeaderPtr header = offset_ptr(start, i * program_header_size, ProgramHeader);

        match_result(
            ProgramHeaderArray__push(&headers, header),
            success_do_nothing(),
            error(code, message)
            {
                __d_println(PANIC, cstr(message));
                ;
            }
        );
    }

    return headers;
}

static UserAddress Elf__explode(ConstElfPtr self, ProgramHeaderPtr header)
{
    Address segment_start = header->virtual_address;
    Address page_start = Elf__page_start(segment_start);
    Offset page_offset = segment_start - page_start;
    size_t allocation_size = Elf__page_allocation_size(segment_start, header->memory_size);

    UserAddress page_address = Kernel__alloc_user(page_start, allocation_size, header->flags & Read,
                                                  header->flags & Write, header->flags & Execute);
    UserAddress address = UserAddress__relative(&page_address, page_offset);

    Kernel__memset(page_address.kernel_addr.virtual_addr, 0, allocation_size);
    Kernel__memcopy(offset_addr(self->start, header->segment_offset), address.kernel_addr.virtual_addr,
                    header->file_size);
    if (header->flags & Execute)
    {
        Arch__sync_instruction_memory(address.kernel_addr.virtual_addr, header->file_size);
    }

    return page_address;
}

static void Elf__expand_segment(ConstElfPtr self, ProgramHeaderPtr header, ExpandedSegmentArrayPtr expanded_segments)
{
    __d_printf(PROCESS, "Expanding segment header=%p name=%s\n", header, cstr(self->name));
    if (header->type == Load)
    {
        UserAddress address = Elf__explode(self, header);
        ExpandedSegment segment =
            ExpandedSegment__new(address, Elf__page_allocation_size(header->virtual_address, header->memory_size));
        ExpandedSegmentArray__push(expanded_segments, segment);
    }
}

ExpandedSegmentArray Elf__map(ConstElfPtr self)
{
    ProgramHeaderArray headers = Elf__read_program_headers(self);
    ExpandedSegmentArray expanded_segments = ExpandedSegmentArray__new();
    __d_printf(DEBUG, "Mapping segments %i\n", ProgramHeaderArray__len(&headers));

    for (int i = 0; i < (int)ProgramHeaderArray__len(&headers); i++)
    {
        match_optional(
            ProgramHeaderArray__at(&headers, i),
            some(header)
            {
                Elf__expand_segment(self, header, &expanded_segments);
                ;
            },
            none()
            {
                __d_println(PANIC, "No header");
                ;
            }
        );
    }
    return expanded_segments;
}

Optional(ExpandedSegment) Elf__entry_segment(ConstElfPtr self, ExpandedSegmentArrayPtr expanded_segments)
{
    Address entry = Elf__read_header(self)->entry;
    ExpandedSegmentArray segments = *expanded_segments;

    for_each_fa(
        ExpandedSegmentArray,
        segments,
        i,
        segment,
        {
            Address start = segment.address.virtual_addr;
            Address end = start + segment.size;
            if (entry >= start && entry < end)
            {
                return Some(ExpandedSegment, segment);
            }
        }
    );
    return None(ExpandedSegment);
}
