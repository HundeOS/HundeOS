#include "loader.h"

#include <string.h>

#include "arch.h"
#include "elf.h"

FixedArrayBodyDefine(WuffMappings);
FixedArrayBodyDefine(WuffLoadedObjects);
OptionalDef(Address);
OptionalDef(WuffProgramHeader);
OptionalDef(MemPagePermissions);

static bool WuffDependencyOffset__equals(uint64_t * self, uint64_t * other)
{
    return *self == *other;
}

FixedArrayDefine(uint64_t, WUFF_MAX_DEPENDENCIES, WuffDependencyOffsets, (EqualFunction)WuffDependencyOffset__equals);
FixedArrayStackBodyDefine(WuffDependencyOffsets);

typedef struct WuffWritableRange
{
    uint8_t * address;
    size_t size;
} WuffWritableRange;

OptionalDef(WuffWritableRange);

typedef struct WuffPageRange
{
    Address start;
    size_t size;
} WuffPageRange;

OptionalDef(WuffPageRange);

typedef struct WuffDynamicInfo
{
    Address hash;
    Address string_table;
    Address symbol_table;
    Address rela;
    Address jump_rela;
    size_t string_table_size;
    size_t symbol_entry_size;
    size_t rela_size;
    size_t rela_entry_size;
    size_t jump_rela_size;
    uint64_t plt_relocation_type;
    WuffDependencyOffsets dependencies;
} WuffDynamicInfo;

ResultDef(WuffDynamicInfo);
typedef WuffDynamicInfo * WuffDynamicInfoPtr;
typedef const WuffDynamicInfo * ConstWuffDynamicInfoPtr;

typedef struct WuffObjectMetadata
{
    WuffDynamicInfo dynamic;
    Optional(WuffProgramHeader) relro_header;
} WuffObjectMetadata;

typedef WuffObjectMetadata * WuffObjectMetadataPtr;
typedef const WuffObjectMetadata * ConstWuffObjectMetadataPtr;

OptionalDef(WuffObjectMetadata);

static bool WuffObjectMetadata__equals(WuffObjectMetadataPtr self, WuffObjectMetadataPtr other)
{
    return self == other;
}

FixedArrayDefine(WuffObjectMetadata, WUFF_MAX_OBJECTS, WuffObjectMetadataList,
                 (EqualFunction)WuffObjectMetadata__equals);
FixedArrayStackBodyDefine(WuffObjectMetadataList);
typedef WuffObjectMetadataList * WuffObjectMetadataListPtr;

typedef struct WuffPreparedObject
{
    WuffObject object;
    WuffObjectMetadata metadata;
} WuffPreparedObject;

typedef WuffPreparedObject * WuffPreparedObjectPtr;

ResultDef(WuffPreparedObject);

static BString invalid_image = bstr("Invalid shared object");
static BString unsupported_image = bstr("Unsupported shared object feature");
static BString mapping_failed = bstr("Unable to map shared object");
static BString relocation_failed = bstr("Unable to relocate shared object");
static BString protection_failed = bstr("Unable to protect shared object");
static BString cleanup_failed = bstr("Unable to release shared object mapping");
static BString dependency_not_found = bstr("Unable to load shared object dependency");
static BString missing_entry = bstr("Shared object has no executable entry point");

static WuffPageRange WuffPageRange__new(Address start, size_t size)
{
    return (WuffPageRange){
        .start = start,
        .size = size,
    };
}

static WuffWritableRange WuffWritableRange__new(uint8_t * address, size_t size)
{
    return (WuffWritableRange){
        .address = address,
        .size = size,
    };
}

static WuffMapping WuffMapping__new(ConstWuffMemoryRegionPtr region, size_t size, MemPagePermissions final_permissions)
{
    return (WuffMapping){
        .virtual_address = region->virtual_address,
        .writable_address = region->writable_address,
        .size = size,
        .final_permissions = final_permissions,
    };
}

static WuffDynamicInfo WuffDynamicInfo__new(void)
{
    return (WuffDynamicInfo){
        .dependencies = WuffDependencyOffsets__new(),
    };
}

static WuffObjectMetadata WuffObjectMetadata__new(ConstWuffDynamicInfoPtr dynamic,
                                                  const OptionalPtr(WuffProgramHeader) relro_header)
{
    return (WuffObjectMetadata){
        .dynamic = *dynamic,
        .relro_header = *relro_header,
    };
}

static WuffPreparedObject WuffPreparedObject__new(ConstWuffObjectPtr object, ConstWuffObjectMetadataPtr metadata)
{
    return (WuffPreparedObject){
        .object = *object,
        .metadata = *metadata,
    };
}

static WuffProgramHeader WuffProgramHeader__new(void)
{
    return (WuffProgramHeader){0};
}

static WuffObject WuffObject__new(ConstWuffMemoryOperationsPtr memory)
{
    return (WuffObject){
        .load_bias = 0,
        .entry = 0,
        .mappings = WuffMappings__new(),
        .memory = *memory,
        .symbols = 0,
        .symbol_count = 0,
        .strings = 0,
        .string_size = 0,
    };
}

WuffImage WuffImage__new(ConstBStringPtr name, const uint8_t * data, size_t size)
{
    return (WuffImage){
        .name = *name,
        .data = data,
        .size = size,
    };
}

WuffDependencyOperations WuffDependencyOperations__new(void * context, WuffDependencyProvider load,
                                                       WuffDependencyRelease release)
{
    return (WuffDependencyOperations){
        .context = context,
        .load = load,
        .release = release,
    };
}

bool WuffLoadedObject__equals(WuffLoadedObjectPtr self, WuffLoadedObjectPtr other)
{
    return BString__equals(&self->name, &other->name);
}

static WuffLoadedObject WuffLoadedObject__new(ConstBStringPtr name, WuffObject object)
{
    return (WuffLoadedObject){
        .name = *name,
        .object = object,
    };
}

static WuffGraph WuffGraph__new(void)
{
    return (WuffGraph){
        .objects = WuffLoadedObjects__new(),
    };
}

static WuffSymbol WuffSymbol__new(Address address, size_t size)
{
    return (WuffSymbol){
        .address = address,
        .size = size,
    };
}

bool WuffMapping__equals(WuffMappingPtr self, WuffMappingPtr other)
{
    return self->virtual_address == other->virtual_address;
}

static bool WuffLoader__range_is_valid(size_t total_size, uint64_t offset, uint64_t size)
{
    return offset <= total_size && size <= total_size - offset;
}

static Optional(Address) WuffLoader__add_address(Address left, uint64_t right)
{
    if (right > UINT64_MAX - left)
    {
        return None(Address);
    }
    return Some(Address, left + right);
}

static Optional(Address) WuffLoader__add_signed_address(Address left, int64_t right)
{
    if (right >= 0)
    {
        return WuffLoader__add_address(left, (uint64_t)right);
    }
    uint64_t magnitude = (uint64_t)(-(right + 1)) + 1;
    if (magnitude > left)
    {
        return None(Address);
    }
    return Some(Address, left - magnitude);
}

static Optional(WuffPageRange) WuffLoader__page_range(Address address, size_t size)
{
    Address page_start = address & ~(WUFF_PAGE_SIZE - 1);
    Address offset = address - page_start;
    if (size == 0 || size > SIZE_MAX - offset || offset + size > SIZE_MAX - (WUFF_PAGE_SIZE - 1))
    {
        return None(WuffPageRange);
    }
    return Some(WuffPageRange,
                WuffPageRange__new(page_start, (offset + size + WUFF_PAGE_SIZE - 1) & ~(WUFF_PAGE_SIZE - 1)));
}

static Optional(WuffProgramHeader)
    WuffLoader__program_header(const uint8_t * image, size_t image_size, const WuffElfHeader * header, size_t index)
{
    if (index >= header->program_header_count ||
        index > (UINT64_MAX - header->program_header_offset) / sizeof(WuffProgramHeader))
    {
        return None(WuffProgramHeader);
    }
    uint64_t offset = header->program_header_offset + index * sizeof(WuffProgramHeader);
    if (!WuffLoader__range_is_valid(image_size, offset, sizeof(WuffProgramHeader)))
    {
        return None(WuffProgramHeader);
    }
    WuffProgramHeader program_header;
    memcpy(&program_header, image + offset, sizeof(program_header));
    return Some(WuffProgramHeader, program_header);
}

static bool WuffLoader__header_is_valid(const WuffElfHeader * header, size_t image_size)
{
    return header->identification[0] == 0x7f && header->identification[1] == 'E' && header->identification[2] == 'L' &&
           header->identification[3] == 'F' && header->identification[4] == WUFF_ELF_CLASS_64 &&
           header->identification[5] == WUFF_ELF_LITTLE_ENDIAN &&
           header->identification[6] == WUFF_ELF_CURRENT_VERSION && header->type == WUFF_ELF_SHARED_OBJECT &&
           header->machine == WuffArch__machine() && header->version == WUFF_ELF_CURRENT_VERSION &&
           header->header_size == sizeof(WuffElfHeader) &&
           header->program_header_entry_size == sizeof(WuffProgramHeader) && header->program_header_count > 0 &&
           header->program_header_count <= WUFF_MAX_PROGRAM_HEADERS &&
           WuffLoader__range_is_valid(image_size, header->program_header_offset,
                                      (uint64_t)header->program_header_count * sizeof(WuffProgramHeader));
}

static Optional(MemPagePermissions) WuffLoader__permissions(uint32_t flags)
{
    if ((flags & ~(WUFF_PROGRAM_READ | WUFF_PROGRAM_WRITE | WUFF_PROGRAM_EXECUTE)) != 0 ||
        (flags & WUFF_PROGRAM_READ) == 0 || ((flags & WUFF_PROGRAM_WRITE) != 0 && (flags & WUFF_PROGRAM_EXECUTE) != 0))
    {
        return None(MemPagePermissions);
    }
    if ((flags & WUFF_PROGRAM_EXECUTE) != 0)
    {
        return Some(MemPagePermissions, MEM_PAGE_READ_EXECUTE);
    }
    if ((flags & WUFF_PROGRAM_WRITE) != 0)
    {
        return Some(MemPagePermissions, MEM_PAGE_READ_WRITE);
    }
    return Some(MemPagePermissions, MEM_PAGE_READ_ONLY);
}

static bool WuffLoader__program_header_is_valid(const WuffProgramHeader * header, size_t image_size)
{
    if (header->type == WUFF_PROGRAM_TLS)
    {
        return false;
    }
    if (header->type == WUFF_PROGRAM_INTERPRETER)
    {
        return header->file_size > 0 && header->file_size == header->memory_size &&
               WuffLoader__range_is_valid(image_size, header->offset, header->file_size);
    }
    if (header->type == WUFF_PROGRAM_GNU_RELRO)
    {
        return header->memory_size > 0;
    }
    if (header->type != WUFF_PROGRAM_LOAD && header->type != WUFF_PROGRAM_DYNAMIC)
    {
        return true;
    }
    if (header->memory_size == 0 || header->file_size > header->memory_size ||
        !WuffLoader__range_is_valid(image_size, header->offset, header->file_size))
    {
        return false;
    }
    if (header->alignment > 1 && ((header->alignment & (header->alignment - 1)) != 0 ||
                                  header->virtual_address % header->alignment != header->offset % header->alignment))
    {
        return false;
    }
    return true;
}

static bool WuffLoader__mapping_overlaps(ConstWuffObjectPtr self, Address address, size_t size)
{
    if (size == 0 || address > UINT64_MAX - size)
    {
        return true;
    }
    Address end = address + size;
    for_each_fa(
        WuffMappings,
        self->mappings,
        index,
        mapping,
        {
            if (mapping.virtual_address > UINT64_MAX - mapping.size)
            {
                return true;
            }
            Address mapping_end = mapping.virtual_address + mapping.size;
            if (mapping.virtual_address < end && address < mapping_end)
            {
                return true;
            }
        }
    );
    return false;
}

static Optional(WuffWritableRange) WuffObject__writable_range(ConstWuffObjectPtr self, Address address, size_t size)
{
    if (size == 0 || address > UINT64_MAX - size)
    {
        return None(WuffWritableRange);
    }
    Address end = address + size;
    for_each_fa(
        WuffMappings,
        self->mappings,
        index,
        mapping,
        {
            if (mapping.virtual_address > UINT64_MAX - mapping.size)
            {
                return None(WuffWritableRange);
            }
            Address mapping_end = mapping.virtual_address + mapping.size;
            if (address >= mapping.virtual_address && end <= mapping_end)
            {
                return Some(WuffWritableRange,
                            WuffWritableRange__new(
                                mapping.writable_address + (address - mapping.virtual_address), size));
            }
        }
    );
    return None(WuffWritableRange);
}

static bool WuffObject__address_is_executable(ConstWuffObjectPtr self, Address address)
{
    for_each_fa(
        WuffMappings,
        self->mappings,
        index,
        mapping,
        {
            if (mapping.final_permissions == MEM_PAGE_READ_EXECUTE && address >= mapping.virtual_address &&
                address - mapping.virtual_address < mapping.size)
            {
                return true;
            }
        }
    );

    return false;
}

static Result(WuffLoadCode) WuffObject__release_mappings(WuffObjectPtr self)
{
    bool failed = false;
    while (WuffMappings__len(&self->mappings) > 0)
    {
        match_optional(
            WuffMappings__pop(&self->mappings),
            some(mapping)
            {
                if (self->memory.free(self->memory.context, mapping.virtual_address) != MEM_OK)
                {
                    failed = true;
                }
            },
            none_do_nothing()
        );
    }
    if (failed)
    {
        return Error(WuffLoadCode, cleanup_failed, WUFF_LOAD_CLEANUP_FAILED);
    }
    return Success(WuffLoadCode, WUFF_LOAD_OK);
}

Result(WuffLoadCode) WuffObject__dispose(WuffObjectPtr self)
{
    return WuffObject__release_mappings(self);
}

static Result(WuffObject) WuffLoader__fail(WuffObjectPtr object, ConstBStringPtr message, WuffLoadCode code)
{
    match_result(
        WuffObject__release_mappings(object),
        success(_)
        {
            return Error(WuffObject, *message, code);
        },
        error(cleanup_code, cleanup_message)
        {
            return Error(WuffObject, cleanup_message, cleanup_code);
        }
    );
    return Error(WuffObject, *message, code);
}

static Result(WuffPreparedObject)
    WuffLoader__prepare_fail(WuffObjectPtr object, ConstBStringPtr message, WuffLoadCode code)
{
    match_result(
        WuffObject__release_mappings(object),
        success(_)
        {
            return Error(WuffPreparedObject, *message, code);
        },
        error(cleanup_code, cleanup_message)
        {
            return Error(WuffPreparedObject, cleanup_message, cleanup_code);
        }
    );
    return Error(WuffPreparedObject, *message, code);
}

static Result(WuffLoadCode) WuffLoader__record_mapping(WuffObjectPtr object, ConstWuffMemoryRegionPtr region,
                                                       size_t size, MemPagePermissions permissions)
{
    if (region->virtual_address == 0 || region->writable_address == 0 ||
        region->virtual_address % WUFF_PAGE_SIZE != 0 ||
        WuffLoader__mapping_overlaps(object, region->virtual_address, size))
    {
        if (region->virtual_address != 0)
        {
            object->memory.free(object->memory.context, region->virtual_address);
        }
        return Error(WuffLoadCode, mapping_failed, WUFF_LOAD_MAPPING_FAILED);
    }
    WuffMapping mapping = WuffMapping__new(region, size, permissions);
    match_result(
        WuffMappings__push(&object->mappings, mapping),
        success(_)
        {
            memset(region->writable_address, 0, size);
            return Success(WuffLoadCode, WUFF_LOAD_OK);
        },
        error(code, message)
        {
            object->memory.free(object->memory.context, region->virtual_address);
            return Error(WuffLoadCode, message, WUFF_LOAD_NO_MEMORY);
        }
    );
    return Error(WuffLoadCode, mapping_failed, WUFF_LOAD_MAPPING_FAILED);
}

static Result(WuffLoadCode)
    WuffLoader__map_segment(WuffObjectPtr object, const uint8_t * image, const WuffProgramHeader * segment, bool first)
{
    WuffPageRange page_range;
    match_optional(
        WuffLoader__page_range(segment->virtual_address, segment->memory_size),
        some(range)
        {
            page_range = range;
        },
        none()
        {
            return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
        }
    );

    MemPagePermissions permissions;
    match_optional(
        WuffLoader__permissions(segment->flags),
        some(value)
        {
            permissions = value;
        },
        none()
        {
            return Error(WuffLoadCode, unsupported_image, WUFF_LOAD_UNSUPPORTED);
        }
    );

    Result(WuffMemoryRegion) allocation;
    size_t required_alignment = WUFF_PAGE_SIZE;
    Address requested_address = 0;
    if (first)
    {
        required_alignment = segment->alignment > WUFF_PAGE_SIZE ? segment->alignment : WUFF_PAGE_SIZE;
        allocation = object->memory.allocate_aligned(object->memory.context, page_range.size, required_alignment);
    }

    else
    {
        Address requested;
        match_optional(
            WuffLoader__add_address(object->load_bias, page_range.start),
            some(address)
            {
                requested = address;
            },
            none()
            {
                return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
            }
        );
        requested_address = requested;
        allocation = object->memory.allocate_at(object->memory.context, requested_address, page_range.size);
    }

    match_result(
        allocation,
        success(region)
        {
            if (first)
            {
                if (region.virtual_address < page_range.start || region.virtual_address % required_alignment != 0)
                {
                    object->memory.free(object->memory.context, region.virtual_address);
                    return Error(WuffLoadCode, mapping_failed, WUFF_LOAD_MAPPING_FAILED);
                }
                object->load_bias = region.virtual_address - page_range.start;
            }
            else if (region.virtual_address != requested_address)
            {
                object->memory.free(object->memory.context, region.virtual_address);
                return Error(WuffLoadCode, mapping_failed, WUFF_LOAD_MAPPING_FAILED);
            }
            match_result(
                WuffLoader__record_mapping(object, &region, page_range.size, permissions),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(WuffLoadCode, message, code);
                }
            );

            Address loaded_segment;
            match_optional(
                WuffLoader__add_address(object->load_bias, segment->virtual_address),
                some(address)
                {
                    loaded_segment = address;
                },
                none()
                {
                    return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
                }
            );
            if (segment->file_size != 0)
            {
                match_optional(
                    WuffObject__writable_range(object, loaded_segment, segment->file_size),
                    some(destination)
                    {
                        memcpy(destination.address, image + segment->offset, segment->file_size);
                    },
                    none()
                    {
                        return Error(WuffLoadCode, mapping_failed, WUFF_LOAD_MAPPING_FAILED);
                    }
                );
            }
            return Success(WuffLoadCode, WUFF_LOAD_OK);
        },
        error(code, message)
        {
            return Error(WuffLoadCode, message, WUFF_LOAD_NO_MEMORY);
        }
    );
    return Error(WuffLoadCode, mapping_failed, WUFF_LOAD_MAPPING_FAILED);
}

static Result(WuffDynamicInfo)
    WuffLoader__dynamic_info(ConstWuffObjectPtr object, const WuffProgramHeader * dynamic_header)
{
    Address dynamic_address;
    match_optional(
        WuffLoader__add_address(object->load_bias, dynamic_header->virtual_address),
        some(address)
        {
            dynamic_address = address;
        },
        none()
        {
            return Error(WuffDynamicInfo, invalid_image, WUFF_LOAD_INVALID_IMAGE);
        }
    );
    if (dynamic_header->file_size == 0 || dynamic_header->file_size % sizeof(WuffDynamicEntry) != 0)
    {
        return Error(WuffDynamicInfo, invalid_image, WUFF_LOAD_INVALID_IMAGE);
    }

    match_optional(
        WuffObject__writable_range(object, dynamic_address, dynamic_header->file_size),
        some(range)
        {
            WuffDynamicInfo info = WuffDynamicInfo__new();
            size_t count = range.size / sizeof(WuffDynamicEntry);
            for (size_t index = 0; index < count; index++)
            {
                WuffDynamicEntry entry;
                memcpy(&entry, range.address + index * sizeof(entry), sizeof(entry));
                switch (entry.tag)
                {
                case WUFF_DYNAMIC_NULL:
                    return Success(WuffDynamicInfo, info);
                case WUFF_DYNAMIC_NEEDED:
                    match_result(
                        WuffDependencyOffsets__push(&info.dependencies, entry.value),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return Error(WuffDynamicInfo, unsupported_image, WUFF_LOAD_UNSUPPORTED);
                        }
                    );
                    break;
                case WUFF_DYNAMIC_PLT_RELOCATION_SIZE:
                    info.jump_rela_size = entry.value;
                    break;
                case WUFF_DYNAMIC_HASH:
                    info.hash = entry.value;
                    break;
                case WUFF_DYNAMIC_STRING_TABLE:
                    info.string_table = entry.value;
                    break;
                case WUFF_DYNAMIC_SYMBOL_TABLE:
                    info.symbol_table = entry.value;
                    break;
                case WUFF_DYNAMIC_RELA:
                    info.rela = entry.value;
                    break;
                case WUFF_DYNAMIC_RELA_SIZE:
                    info.rela_size = entry.value;
                    break;
                case WUFF_DYNAMIC_RELA_ENTRY_SIZE:
                    info.rela_entry_size = entry.value;
                    break;
                case WUFF_DYNAMIC_STRING_TABLE_SIZE:
                    info.string_table_size = entry.value;
                    break;
                case WUFF_DYNAMIC_SYMBOL_ENTRY_SIZE:
                    info.symbol_entry_size = entry.value;
                    break;
                case WUFF_DYNAMIC_PLT_RELOCATION_TYPE:
                    info.plt_relocation_type = entry.value;
                    break;
                case WUFF_DYNAMIC_JUMP_RELOCATION:
                    info.jump_rela = entry.value;
                    break;
                default:
                    break;
                }
            }
        },
        none_do_nothing()
    );
    return Error(WuffDynamicInfo, invalid_image, WUFF_LOAD_INVALID_IMAGE);
}

static Optional(BString) WuffObject__symbol_name(ConstWuffObjectPtr self, uint32_t offset)
{
    if (offset >= self->string_size)
    {
        return None(BString);
    }
    size_t length = 0;
    while (offset + length < self->string_size && self->strings[offset + length] != '\0')
    {
        length++;
    }
    if (offset + length == self->string_size || length >= BSTRING_MAX_SIZE)
    {
        return None(BString);
    }

    return Some(BString, BString__from_chars(self->strings + offset, (StringPos)length));
}

static Result(Address)
    WuffLoader__relocation_value(ConstWuffGraphPtr graph, ConstWuffObjectPtr object, const WuffRela * relocation)
{
    WuffRelocationKind kind = WuffArch__relocation_kind((uint32_t)relocation->info);
    uint32_t symbol_index = (uint32_t)(relocation->info >> 32);
    if (kind == WUFF_RELOCATION_UNSUPPORTED)
    {
        return Error(Address, relocation_failed, WUFF_LOAD_UNSUPPORTED);
    }

    if (kind == WUFF_RELOCATION_RELATIVE)
    {
        if (symbol_index != 0)
        {
            return Error(Address, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
        }

        match_optional(
            WuffLoader__add_signed_address(object->load_bias, relocation->addend),
            some(value)
            {
                return Success(Address, value);
            },
            none_do_nothing()
        );
        return Error(Address, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
    }
    if (symbol_index == 0 || symbol_index >= object->symbol_count)
    {
        return Error(Address, relocation_failed, WUFF_LOAD_SYMBOL_NOT_FOUND);
    }
    WuffSymbolEntry symbol;
    memcpy(&symbol, &object->symbols[symbol_index], sizeof(symbol));
    uint8_t binding = symbol.information >> 4;
    uint8_t visibility = symbol.other & 3;
    if (binding != WUFF_SYMBOL_GLOBAL && binding != WUFF_SYMBOL_WEAK && binding != WUFF_SYMBOL_LOCAL)
    {
        return Error(Address, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
    }
    if (symbol.section_index == WUFF_SYMBOL_UNDEFINED &&
        (binding == WUFF_SYMBOL_LOCAL || visibility != WUFF_SYMBOL_VISIBILITY_DEFAULT))
    {
        return Error(Address, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
    }
    Address symbol_address = 0;
    bool binds_locally = symbol.section_index != WUFF_SYMBOL_UNDEFINED &&
                         (binding == WUFF_SYMBOL_LOCAL || visibility == WUFF_SYMBOL_VISIBILITY_HIDDEN ||
                          visibility == WUFF_SYMBOL_VISIBILITY_PROTECTED);
    if (binds_locally)
    {
        match_optional(
            WuffLoader__add_address(object->load_bias, symbol.value),
            some(address)
            {
                symbol_address = address;
            },
            none()
            {
                return Error(Address, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
            }
        );
    }
    else
    {
        BString name;
        match_optional(
            WuffObject__symbol_name(object, symbol.name),
            some(value)
            {
                name = value;
            },
            none()
            {
                return Error(Address, relocation_failed, WUFF_LOAD_INVALID_IMAGE);
            }
        );
        match_optional(
            WuffGraph__find_symbol(graph, &name),
            some(resolved)
            {
                symbol_address = resolved.address;
            },
            none()
            {
                if (binding != WUFF_SYMBOL_WEAK)
                {
                    return Error(Address, relocation_failed, WUFF_LOAD_SYMBOL_NOT_FOUND);
                }
            }
        );
    }
    match_optional(
        WuffLoader__add_signed_address(symbol_address, relocation->addend),
        some(value)
        {
            return Success(Address, value);
        },
        none_do_nothing()
    );
    return Error(Address, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
}

static Result(WuffLoadCode) WuffLoader__apply_relocation_table(ConstWuffGraphPtr graph, WuffObjectPtr object,
                                                               Address table_offset, size_t table_size)
{
    if (table_size == 0)
    {
        return Success(WuffLoadCode, WUFF_LOAD_OK);
    }
    if (table_offset == 0 || table_size % sizeof(WuffRela) != 0)
    {
        return Error(WuffLoadCode, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
    }
    Address rela_address;
    match_optional(
        WuffLoader__add_address(object->load_bias, table_offset),
        some(address)
        {
            rela_address = address;
        },
        none()
        {
            return Error(WuffLoadCode, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
        }
    );
    match_optional(
        WuffObject__writable_range(object, rela_address, table_size),
        some(table)
        {
            size_t count = table.size / sizeof(WuffRela);
            for (size_t index = 0; index < count; index++)
            {
                WuffRela relocation;
                memcpy(&relocation, table.address + index * sizeof(relocation), sizeof(relocation));
                Address target_address;
                match_optional(
                    WuffLoader__add_address(object->load_bias, relocation.offset),
                    some(address)
                    {
                        target_address = address;
                    },
                    none()
                    {
                        return Error(WuffLoadCode, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
                    }
                );
                match_result(
                    WuffLoader__relocation_value(graph, object, &relocation),
                    success(value)
                    {
                        match_optional(
                            WuffObject__writable_range(object, target_address, sizeof(Address)),
                            some(target)
                            {
                                memcpy(target.address, &value, sizeof(value));
                            },
                            none()
                            {
                                return Error(WuffLoadCode, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
                            }
                        );
                    },
                    error(code, message)
                    {
                        return Error(WuffLoadCode, message, code);
                    }
                );
            }
            return Success(WuffLoadCode, WUFF_LOAD_OK);
        },
        none()
        {
            return Error(WuffLoadCode, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
        }
    );
    return Error(WuffLoadCode, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
}

static Result(WuffLoadCode)
    WuffLoader__apply_relocations(ConstWuffGraphPtr graph, WuffObjectPtr object, ConstWuffDynamicInfoPtr dynamic)
{
    if ((dynamic->rela_size != 0 || dynamic->jump_rela_size != 0) && dynamic->rela_entry_size != sizeof(WuffRela))
    {
        return Error(WuffLoadCode, relocation_failed, WUFF_LOAD_RELOCATION_FAILED);
    }
    if (dynamic->jump_rela_size != 0 && dynamic->plt_relocation_type != WUFF_DYNAMIC_RELA)
    {
        return Error(WuffLoadCode, relocation_failed, WUFF_LOAD_UNSUPPORTED);
    }

    match_result(
        WuffLoader__apply_relocation_table(graph, object, dynamic->rela, dynamic->rela_size),
        success_do_nothing(),
        error(code, message)
        {
            return Error(WuffLoadCode, message, code);
        }
    );
    return WuffLoader__apply_relocation_table(graph, object, dynamic->jump_rela, dynamic->jump_rela_size);
}

static Result(WuffLoadCode) WuffLoader__load_symbols(WuffObjectPtr object, ConstWuffDynamicInfoPtr dynamic)
{
    if (dynamic->hash == 0 && dynamic->symbol_table == 0 && dynamic->string_table == 0)
    {
        return Success(WuffLoadCode, WUFF_LOAD_OK);
    }
    if (dynamic->hash == 0 || dynamic->symbol_table == 0 || dynamic->string_table == 0 ||
        dynamic->string_table_size == 0 || dynamic->symbol_entry_size != sizeof(WuffSymbolEntry))
    {
        return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
    }

    Address hash_address;
    Address symbol_address;
    Address string_address;
    match_optional(
        WuffLoader__add_address(object->load_bias, dynamic->hash),
        some(address)
        {
            hash_address = address;
        },
        none()
        {
            return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
        }
    );
    match_optional(
        WuffLoader__add_address(object->load_bias, dynamic->symbol_table),
        some(address)
        {
            symbol_address = address;
        },
        none()
        {
            return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
        }
    );
    match_optional(
        WuffLoader__add_address(object->load_bias, dynamic->string_table),
        some(address)
        {
            string_address = address;
        },
        none()
        {
            return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
        }
    );

    uint32_t bucket_count;
    uint32_t symbol_count;
    match_optional(
        WuffObject__writable_range(object, hash_address, sizeof(uint32_t) * 2),
        some(hash)
        {
            memcpy(&bucket_count, hash.address, sizeof(bucket_count));
            memcpy(&symbol_count, hash.address + sizeof(uint32_t), sizeof(symbol_count));
        },
        none()
        {
            return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
        }
    );
    size_t hash_entries = (size_t)bucket_count + symbol_count + 2;
    if (bucket_count == 0 || symbol_count == 0 || hash_entries > SIZE_MAX / sizeof(uint32_t))
    {
        return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
    }
    size_t hash_size = hash_entries * sizeof(uint32_t);
    if (is_nothing(WuffObject__writable_range(object, hash_address, hash_size)))
    {
        return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
    }

    match_optional(
        WuffObject__writable_range(object, symbol_address, symbol_count * sizeof(WuffSymbolEntry)),
        some(symbols)
        {
            object->symbols = (const WuffSymbolEntry *)symbols.address;
            object->symbol_count = symbol_count;
        },
        none()
        {
            return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
        }
    );
    match_optional(
        WuffObject__writable_range(object, string_address, dynamic->string_table_size),
        some(strings)
        {
            object->strings = (const char *)strings.address;
            object->string_size = strings.size;
        },
        none()
        {
            return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
        }
    );
    if (object->strings[0] != '\0')
    {
        return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
    }
    for (size_t index = 0; index < object->symbol_count; index++)
    {
        WuffSymbolEntry symbol;
        memcpy(&symbol, &object->symbols[index], sizeof(symbol));
        if (symbol.name >= object->string_size ||
            memchr(object->strings + symbol.name, '\0', object->string_size - symbol.name) == 0)
        {
            return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
        }
        if (symbol.section_index != WUFF_SYMBOL_UNDEFINED)
        {
            Address loaded_symbol;
            match_optional(
                WuffLoader__add_address(object->load_bias, symbol.value),
                some(address)
                {
                    loaded_symbol = address;
                },
                none()
                {
                    return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
                }
            );
            size_t symbol_size = symbol.size == 0 ? 1 : symbol.size;
            if (is_nothing(WuffObject__writable_range(object, loaded_symbol, symbol_size)))
            {
                return Error(WuffLoadCode, invalid_image, WUFF_LOAD_INVALID_IMAGE);
            }
        }
    }
    return Success(WuffLoadCode, WUFF_LOAD_OK);
}

static Result(WuffLoadCode) WuffLoader__protect(WuffObjectPtr object)
{
    for_each_fa(
        WuffMappings,
        object->mappings,
        index,
        mapping,
        {
            if (object->memory.protect(object->memory.context, mapping.virtual_address, mapping.size,
                                       mapping.final_permissions) != MEM_OK)
            {
                return Error(WuffLoadCode, protection_failed, WUFF_LOAD_PROTECTION_FAILED);
            }
        }
    );
    return Success(WuffLoadCode, WUFF_LOAD_OK);
}

static Result(WuffLoadCode)
    WuffLoader__protect_relro(WuffObjectPtr object, const OptionalPtr(WuffProgramHeader) relro_header)
{
    match_optional(
        *relro_header,
        some(header)
        {
            Address address;
            match_optional(
                WuffLoader__add_address(object->load_bias, header.virtual_address),
                some(loaded)
                {
                    address = loaded;
                },
                none()
                {
                    return Error(WuffLoadCode, protection_failed, WUFF_LOAD_PROTECTION_FAILED);
                }
            );
            WuffPageRange pages;
            match_optional(
                WuffLoader__page_range(address, header.memory_size),
                some(range)
                {
                    pages = range;
                },
                none()
                {
                    return Error(WuffLoadCode, protection_failed, WUFF_LOAD_PROTECTION_FAILED);
                }
            );
            if (is_nothing(WuffObject__writable_range(object, pages.start, pages.size)) ||
                object->memory.protect(object->memory.context, pages.start, pages.size, MEM_PAGE_READ_ONLY) != MEM_OK)
            {
                return Error(WuffLoadCode, protection_failed, WUFF_LOAD_PROTECTION_FAILED);
            }
        },
        none_do_nothing()
    );
    return Success(WuffLoadCode, WUFF_LOAD_OK);
}

static Result(WuffPreparedObject)
    WuffLoader__prepare(const uint8_t * image, size_t image_size, ConstWuffMemoryOperationsPtr memory, bool is_root)
{
    if (memory->allocate_aligned == 0 || memory->allocate_at == 0 || memory->protect == 0 || memory->free == 0 ||
        image_size < sizeof(WuffElfHeader))
    {
        return Error(WuffPreparedObject, invalid_image, WUFF_LOAD_INVALID_IMAGE);
    }
    WuffObject object = WuffObject__new(memory);

    WuffElfHeader header;
    memcpy(&header, image, sizeof(header));
    if (!WuffLoader__header_is_valid(&header, image_size))
    {
        return Error(WuffPreparedObject, invalid_image, WUFF_LOAD_INVALID_IMAGE);
    }

    Optional(WuffProgramHeader) dynamic_header = None(WuffProgramHeader);
    Optional(WuffProgramHeader) interpreter_header = None(WuffProgramHeader);
    Optional(WuffProgramHeader) relro_header = None(WuffProgramHeader);
    Optional(WuffProgramHeader) first_load = None(WuffProgramHeader);
    Address first_start = UINT64_MAX;
    for (size_t index = 0; index < header.program_header_count; index++)
    {
        match_optional(
            WuffLoader__program_header(image, image_size, &header, index),
            some(program_header)
            {
                if (!WuffLoader__program_header_is_valid(&program_header, image_size))
                {
                    return Error(WuffPreparedObject, invalid_image, WUFF_LOAD_INVALID_IMAGE);
                }
                if (program_header.type == WUFF_PROGRAM_DYNAMIC)
                {
                    if (is_something(dynamic_header))
                    {
                        return Error(WuffPreparedObject, invalid_image, WUFF_LOAD_INVALID_IMAGE);
                    }
                    dynamic_header = Some(WuffProgramHeader, program_header);
                }
                if (program_header.type == WUFF_PROGRAM_INTERPRETER)
                {
                    if (is_something(interpreter_header))
                    {
                        return Error(WuffPreparedObject, invalid_image, WUFF_LOAD_INVALID_IMAGE);
                    }
                    interpreter_header = Some(WuffProgramHeader, program_header);
                }
                if (program_header.type == WUFF_PROGRAM_GNU_RELRO)
                {
                    if (is_something(relro_header))
                    {
                        return Error(WuffPreparedObject, invalid_image, WUFF_LOAD_INVALID_IMAGE);
                    }
                    relro_header = Some(WuffProgramHeader, program_header);
                }
                if (program_header.type == WUFF_PROGRAM_LOAD)
                {
                    Address start = program_header.virtual_address & ~(WUFF_PAGE_SIZE - 1);
                    if (start < first_start)
                    {
                        first_start = start;
                        first_load = Some(WuffProgramHeader, program_header);
                    }
                }
            },
            none()
            {
                return Error(WuffPreparedObject, invalid_image, WUFF_LOAD_INVALID_IMAGE);
            }
        );
    }

    if (is_nothing(first_load))
    {
        return Error(WuffPreparedObject, invalid_image, WUFF_LOAD_INVALID_IMAGE);
    }

    match_optional(
        interpreter_header,
        some(program_header)
        {
            if (!is_root || is_nothing(dynamic_header) || program_header.file_size != sizeof(WUFF_INTERPRETER_PATH) ||
                memcmp(image + program_header.offset, WUFF_INTERPRETER_PATH, sizeof(WUFF_INTERPRETER_PATH)) != 0)
            {
                return Error(WuffPreparedObject, invalid_image, WUFF_LOAD_INVALID_IMAGE);
            }
        },
        none_do_nothing()
    );

    match_optional(
        first_load,
        some(segment)
        {
            match_result(
                WuffLoader__map_segment(&object, image, &segment, true),
                success_do_nothing(),
                error(code, message)
                {
                    return WuffLoader__prepare_fail(&object, &message, code);
                }
            );
        },
        none_do_nothing()
    );

    bool first_load_skipped = false;
    for (size_t index = 0; index < header.program_header_count; index++)
    {
        WuffProgramHeader program_header =
            unwrap_optional(WuffLoader__program_header(image, image_size, &header, index), WuffProgramHeader__new());
        Address start = program_header.virtual_address & ~(WUFF_PAGE_SIZE - 1);
        if (program_header.type != WUFF_PROGRAM_LOAD)
        {
            continue;
        }

        if (!first_load_skipped && start == first_start)
        {
            first_load_skipped = true;
            continue;
        }
        match_result(
            WuffLoader__map_segment(&object, image, &program_header, false),
            success_do_nothing(),
            error(code, message)
            {
                return WuffLoader__prepare_fail(&object, &message, code);
            }
        );
    }

    if (header.entry != 0)
    {
        match_optional(
            WuffLoader__add_address(object.load_bias, header.entry),
            some(entry)
            {
                if (!WuffObject__address_is_executable(&object, entry))
                {
                    return WuffLoader__prepare_fail(&object, &invalid_image, WUFF_LOAD_INVALID_IMAGE);
                }
                object.entry = entry;
            },
            none()
            {
                return WuffLoader__prepare_fail(&object, &invalid_image, WUFF_LOAD_INVALID_IMAGE);
            }
        );
    }

    WuffDynamicInfo dynamic = WuffDynamicInfo__new();
    if (is_something(dynamic_header))
    {
        match_optional(
            dynamic_header,
            some(program_header)
            {
                match_result(
                    WuffLoader__dynamic_info(&object, &program_header),
                    success(info)
                    {
                        dynamic = info;
                    },
                    error(code, message)
                    {
                        return WuffLoader__prepare_fail(&object, &message, code);
                    }
                );
            },
            none_do_nothing()
        );
        match_result(
            WuffLoader__load_symbols(&object, &dynamic),
            success_do_nothing(),
            error(code, message)
            {
                return WuffLoader__prepare_fail(&object, &message, code);
            }
        );
    }

    WuffObjectMetadata metadata = WuffObjectMetadata__new(&dynamic, &relro_header);
    return Success(WuffPreparedObject, WuffPreparedObject__new(&object, &metadata));
}

static Result(WuffLoadCode)
    WuffLoader__finalize(ConstWuffGraphPtr graph, WuffObjectPtr object, WuffObjectMetadataPtr metadata)
{
    match_result(
        WuffLoader__apply_relocations(graph, object, &metadata->dynamic),
        success_do_nothing(),
        error(code, message)
        {
            return Error(WuffLoadCode, message, code);
        }
    );
    match_result(
        WuffLoader__protect(object),
        success_do_nothing(),
        error(code, message)
        {
            return Error(WuffLoadCode, message, code);
        }
    );
    return WuffLoader__protect_relro(object, &metadata->relro_header);
}

Result(WuffObject) WuffLoader__load(const uint8_t * image, size_t image_size, ConstWuffMemoryOperationsPtr memory)
{
    WuffPreparedObject prepared;
    match_result(
        WuffLoader__prepare(image, image_size, memory, false),
        success(value)
        {
            prepared = value;
        },
        error(code, message)
        {
            return Error(WuffObject, message, code);
        }
    );
    if (WuffDependencyOffsets__len(&prepared.metadata.dynamic.dependencies) != 0)
    {
        return WuffLoader__fail(&prepared.object, &unsupported_image, WUFF_LOAD_UNSUPPORTED);
    }

    WuffGraph graph = WuffGraph__new();
    BString root_name = empty_bstr();
    WuffLoadedObject loaded = WuffLoadedObject__new(&root_name, prepared.object);
    match_result(
        WuffLoadedObjects__push(&graph.objects, loaded),
        success_do_nothing(),
        error(code, message)
        {
            return WuffLoader__fail(&prepared.object, &message, WUFF_LOAD_NO_MEMORY);
        }
    );
    WuffLoadedObjectPtr root;
    match_optional(
        WuffLoadedObjects__ptr_at(&graph.objects, 0),
        some(value)
        {
            root = value;
        },
        none()
        {
            return WuffLoader__fail(&prepared.object, &invalid_image, WUFF_LOAD_INVALID_IMAGE);
        }
    );
    match_result(
        WuffLoader__finalize(&graph, &root->object, &prepared.metadata),
        success_do_nothing(),
        error(code, message)
        {
            return WuffLoader__fail(&root->object, &message, code);
        }
    );
    return Success(WuffObject, root->object);
}

static bool WuffObject__symbol_name_equals(ConstWuffObjectPtr self, uint32_t offset, ConstBStringPtr name)
{
    if (offset >= self->string_size)
    {
        return false;
    }
    size_t expected_length = (size_t)BString__length(name);
    if (expected_length >= self->string_size - offset)
    {
        return false;
    }

    for (size_t index = 0; index < expected_length; index++)
    {
        if (self->strings[offset + index] != BString__char_at_or(name, index, '\0'))
        {
            return false;
        }
    }

    return self->strings[offset + expected_length] == '\0';
}

Optional(WuffSymbol) WuffObject__find_symbol(ConstWuffObjectPtr self, ConstBStringPtr name)
{
    for (size_t index = 1; index < self->symbol_count; index++)
    {
        WuffSymbolEntry symbol;
        memcpy(&symbol, &self->symbols[index], sizeof(symbol));
        uint8_t binding = symbol.information >> 4;
        uint8_t visibility = symbol.other & 3;
        if (symbol.section_index != WUFF_SYMBOL_UNDEFINED &&
            (binding == WUFF_SYMBOL_GLOBAL || binding == WUFF_SYMBOL_WEAK) &&
            (visibility == WUFF_SYMBOL_VISIBILITY_DEFAULT || visibility == WUFF_SYMBOL_VISIBILITY_PROTECTED) &&
            WuffObject__symbol_name_equals(self, symbol.name, name))
        {
            Address address;
            match_optional(
                WuffLoader__add_address(self->load_bias, symbol.value),
                some(value)
                {
                    address = value;
                },
                none()
                {
                    return None(WuffSymbol);
                }
            );
            return Some(WuffSymbol, WuffSymbol__new(address, symbol.size));
        }
    }
    return None(WuffSymbol);
}

Optional(WuffSymbol) WuffGraph__find_symbol(ConstWuffGraphPtr self, ConstBStringPtr name)
{
    for_each_fa(
        WuffLoadedObjects,
        self->objects,
        index,
        loaded,
        {
            match_optional(
                WuffObject__find_symbol(&loaded.object, name),
                some(symbol)
                {
                    return Some(WuffSymbol, symbol);
                },
                none_do_nothing()
            );
        }
    );
    return None(WuffSymbol);
}

Result(Address) WuffGraph__entry(ConstWuffGraphPtr self)
{
    for_each_fa(
        WuffLoadedObjects,
        self->objects,
        index,
        root,
        {
            if (index == 0 && root.object.entry != 0)
            {
                return Success(Address, root.object.entry);
            }
            break;
        }
    );
    return Error(Address, missing_entry, WUFF_LOAD_INVALID_IMAGE);
}

Result(WuffLoadCode) WuffGraph__dispose(WuffGraphPtr self)
{
    bool failed = false;
    while (WuffLoadedObjects__len(&self->objects) > 0)
    {
        match_optional(
            WuffLoadedObjects__pop(&self->objects),
            some(loaded)
            {
                match_result(
                    WuffObject__dispose(&loaded.object),
                    success_do_nothing(),
                    error(code, message)
                    {
                        failed = true;
                    }
                );
            },
            none_do_nothing()
        );
    }
    if (failed)
    {
        return Error(WuffLoadCode, cleanup_failed, WUFF_LOAD_CLEANUP_FAILED);
    }

    return Success(WuffLoadCode, WUFF_LOAD_OK);
}

static Result(WuffGraph) WuffLoader__graph_fail(WuffGraphPtr graph, ConstBStringPtr message, WuffLoadCode code)
{
    match_result(
        WuffGraph__dispose(graph),
        success(_)
        {
            return Error(WuffGraph, *message, code);
        },
        error(cleanup_code, cleanup_message)
        {
            return Error(WuffGraph, cleanup_message, cleanup_code);
        }
    );
    return Error(WuffGraph, *message, code);
}

static bool WuffLoadedObject__name_matches(WuffLoadedObjectPtr loaded, void * context)
{
    return BString__equals(&loaded->name, context);
}

static bool WuffGraph__contains(WuffGraphPtr self, ConstBStringPtr name)
{
    match_find_result(
        WuffLoadedObjects__find(&self->objects, WuffLoadedObject__name_matches, (void *)name),
        some(found)
        {
            return true;
        },
        none()
        {
            return false;
        }
    );
}

static Optional(BString) WuffLoader__dependency_name(ConstWuffObjectPtr object, uint64_t offset)
{
    if (offset > UINT32_MAX)
    {
        return None(BString);
    }
    return WuffObject__symbol_name(object, (uint32_t)offset);
}

static Result(WuffLoadCode) WuffLoader__add_prepared(WuffGraphPtr graph, WuffObjectMetadataListPtr metadata,
                                                     ConstBStringPtr name, WuffPreparedObjectPtr prepared)
{
    if (WuffLoadedObjects__len(&graph->objects) >= WUFF_MAX_OBJECTS)
    {
        return Error(WuffLoadCode, unsupported_image, WUFF_LOAD_UNSUPPORTED);
    }
    WuffLoadedObject loaded = WuffLoadedObject__new(name, prepared->object);
    match_result(
        WuffObjectMetadataList__push(metadata, prepared->metadata),
        success_do_nothing(),
        error(code, message)
        {
            return Error(WuffLoadCode, message, WUFF_LOAD_NO_MEMORY);
        }
    );
    match_result(
        WuffLoadedObjects__push(&graph->objects, loaded),
        success_do_nothing(),
        error(code, message)
        {
            return Error(WuffLoadCode, message, WUFF_LOAD_NO_MEMORY);
        }
    );
    return Success(WuffLoadCode, WUFF_LOAD_OK);
}

Result(WuffGraph) WuffLoader__load_graph(ConstWuffImagePtr root, ConstWuffMemoryOperationsPtr memory,
                                         ConstWuffDependencyOperationsPtr dependencies)
{
    if (BString__is_empty(&root->name) || root->data == 0 || dependencies->load == 0 || dependencies->release == 0)
    {
        return Error(WuffGraph, invalid_image, WUFF_LOAD_INVALID_IMAGE);
    }
    WuffGraph graph = WuffGraph__new();
    WuffObjectMetadataList metadata = WuffObjectMetadataList__new();
    WuffPreparedObject prepared;
    match_result(
        WuffLoader__prepare(root->data, root->size, memory, true),
        success(value)
        {
            prepared = value;
        },
        error(code, message)
        {
            return Error(WuffGraph, message, code);
        }
    );
    match_result(
        WuffLoader__add_prepared(&graph, &metadata, &root->name, &prepared),
        success_do_nothing(),
        error(code, message)
        {
            match_result(
                WuffObject__dispose(&prepared.object),
                success(_)
                {
                    return Error(WuffGraph, message, code);
                },
                error(cleanup_code, cleanup_message)
                {
                    return Error(WuffGraph, cleanup_message, cleanup_code);
                }
            );
        }
    );

    size_t object_index = 0;
    while (object_index < WuffLoadedObjects__len(&graph.objects))
    {
        WuffLoadedObjectPtr loaded;
        WuffObjectMetadataPtr object_metadata;
        match_optional(
            WuffLoadedObjects__ptr_at(&graph.objects, (int64_t)object_index),
            some(value)
            {
                loaded = value;
            },
            none()
            {
                return WuffLoader__graph_fail(&graph, &invalid_image, WUFF_LOAD_INVALID_IMAGE);
            }
        );
        match_optional(
            WuffObjectMetadataList__ptr_at(&metadata, (int64_t)object_index),
            some(value)
            {
                object_metadata = value;
            },
            none()
            {
                return WuffLoader__graph_fail(&graph, &invalid_image, WUFF_LOAD_INVALID_IMAGE);
            }
        );
        for_each_fa_ptr(
            WuffDependencyOffsets,
            &object_metadata->dynamic.dependencies,
            dependency_index,
            dependency_offset,
            {
                BString dependency_name;
                match_optional(
                    WuffLoader__dependency_name(&loaded->object, dependency_offset),
                    some(name)
                    {
                        dependency_name = name;
                    },
                    none()
                    {
                        return WuffLoader__graph_fail(&graph, &invalid_image, WUFF_LOAD_INVALID_IMAGE);
                    }
                );
                if (WuffGraph__contains(&graph, &dependency_name))
                {
                    continue;
                }
                WuffImage image;
                match_result(
                    dependencies->load(dependencies->context, &dependency_name),
                    success(value)
                    {
                        image = value;
                    },
                    error(code, message)
                    {
                        return WuffLoader__graph_fail(&graph, &message, code);
                    }
                );
                if (!BString__equals(&image.name, &dependency_name) || image.data == 0)
                {
                    dependencies->release(dependencies->context, &image);
                    return WuffLoader__graph_fail(&graph, &dependency_not_found, WUFF_LOAD_DEPENDENCY_NOT_FOUND);
                }
                WuffPreparedObject dependency;
                match_result(
                    WuffLoader__prepare(image.data, image.size, memory, false),
                    success(value)
                    {
                        dependency = value;
                    },
                    error(code, message)
                    {
                        dependencies->release(dependencies->context, &image);
                        return WuffLoader__graph_fail(&graph, &message, code);
                    }
                );
                dependencies->release(dependencies->context, &image);
                match_result(
                    WuffLoader__add_prepared(&graph, &metadata, &dependency_name, &dependency),
                    success_do_nothing(),
                    error(code, message)
                    {
                        match_result(
                            WuffObject__dispose(&dependency.object),
                            success(_)
                            {
                                return WuffLoader__graph_fail(&graph, &message, code);
                            },
                            error(cleanup_code, cleanup_message)
                            {
                                return WuffLoader__graph_fail(&graph, &cleanup_message, cleanup_code);
                            }
                        );
                    }
                );
            }
        );

        object_index++;
    }

    for (size_t index = 0; index < WuffLoadedObjects__len(&graph.objects); index++)
    {
        WuffLoadedObjectPtr loaded;
        WuffObjectMetadataPtr object_metadata;
        match_optional(
            WuffLoadedObjects__ptr_at(&graph.objects, (int64_t)index),
            some(value)
            {
                loaded = value;
            },
            none()
            {
                return WuffLoader__graph_fail(&graph, &invalid_image, WUFF_LOAD_INVALID_IMAGE);
            }
        );
        match_optional(
            WuffObjectMetadataList__ptr_at(&metadata, (int64_t)index),
            some(value)
            {
                object_metadata = value;
            },
            none()
            {
                return WuffLoader__graph_fail(&graph, &invalid_image, WUFF_LOAD_INVALID_IMAGE);
            }
        );
        match_result(
            WuffLoader__finalize(&graph, &loaded->object, object_metadata),
            success_do_nothing(),
            error(code, message)
            {
                return WuffLoader__graph_fail(&graph, &message, code);
            }
        );
    }

    return Success(WuffGraph, graph);
}
