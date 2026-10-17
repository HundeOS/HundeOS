#ifndef __USER_LIB_WUFF_ELF_H
#define __USER_LIB_WUFF_ELF_H

#include <stdint.h>

typedef struct WuffElfHeader
{
    uint8_t identification[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} WuffElfHeader;

typedef struct WuffProgramHeader
{
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} WuffProgramHeader;

typedef struct WuffDynamicEntry
{
    int64_t tag;
    uint64_t value;
} WuffDynamicEntry;

typedef struct WuffRela
{
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} WuffRela;

typedef struct WuffSymbolEntry
{
    uint32_t name;
    uint8_t information;
    uint8_t other;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
} WuffSymbolEntry;

enum
{
    WUFF_ELF_CLASS_64 = 2,
    WUFF_ELF_LITTLE_ENDIAN = 1,
    WUFF_ELF_CURRENT_VERSION = 1,
    WUFF_ELF_SHARED_OBJECT = 3,
    WUFF_PROGRAM_LOAD = 1,
    WUFF_PROGRAM_DYNAMIC = 2,
    WUFF_PROGRAM_INTERPRETER = 3,
    WUFF_PROGRAM_TLS = 7,
    WUFF_PROGRAM_GNU_RELRO = 0x6474e552,
    WUFF_PROGRAM_EXECUTE = 1,
    WUFF_PROGRAM_WRITE = 2,
    WUFF_PROGRAM_READ = 4,
    WUFF_DYNAMIC_NULL = 0,
    WUFF_DYNAMIC_NEEDED = 1,
    WUFF_DYNAMIC_PLT_RELOCATION_SIZE = 2,
    WUFF_DYNAMIC_HASH = 4,
    WUFF_DYNAMIC_STRING_TABLE = 5,
    WUFF_DYNAMIC_SYMBOL_TABLE = 6,
    WUFF_DYNAMIC_RELA = 7,
    WUFF_DYNAMIC_RELA_SIZE = 8,
    WUFF_DYNAMIC_RELA_ENTRY_SIZE = 9,
    WUFF_DYNAMIC_STRING_TABLE_SIZE = 10,
    WUFF_DYNAMIC_SYMBOL_ENTRY_SIZE = 11,
    WUFF_DYNAMIC_PLT_RELOCATION_TYPE = 20,
    WUFF_DYNAMIC_JUMP_RELOCATION = 23,
    WUFF_SYMBOL_UNDEFINED = 0,
    WUFF_SYMBOL_LOCAL = 0,
    WUFF_SYMBOL_GLOBAL = 1,
    WUFF_SYMBOL_WEAK = 2,
    WUFF_SYMBOL_VISIBILITY_DEFAULT = 0,
    WUFF_SYMBOL_VISIBILITY_INTERNAL = 1,
    WUFF_SYMBOL_VISIBILITY_HIDDEN = 2,
    WUFF_SYMBOL_VISIBILITY_PROTECTED = 3,
};

#endif
