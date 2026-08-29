#ifndef __KERNEL_BIN_EMBEDDED_FS_H
#define __KERNEL_BIN_EMBEDDED_FS_H

#include <stdint.h>

#include "../address.h"
#include "../types/bstring_additional_types.h"
#include "../types/optional.h"

#define EMBEDDED_FS_MAGIC 0x31534645
#define EMBEDDED_FS_VERSION 1

typedef struct EmbeddedFsHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t entry_count;
    uint32_t index_offset;
    uint32_t names_offset;
    uint32_t data_offset;
    uint32_t blob_size;
    uint32_t reserved0;
    uint32_t reserved1;
}
EmbeddedFsHeader;

typedef EmbeddedFsHeader * EmbeddedFsHeaderPtr;

typedef struct EmbeddedFsEntry
{
    uint64_t data_offset;
    uint64_t data_len;
    uint32_t name_offset;
    uint32_t name_len;
    uint32_t flags;
    uint32_t reserved0;
}
EmbeddedFsEntry;

typedef EmbeddedFsEntry * EmbeddedFsEntryPtr;

typedef struct EmbeddedFs
{
    Address start;
    Address end;
}
EmbeddedFs;

typedef EmbeddedFs * EmbeddedFsPtr;

typedef struct EmbeddedFsFile
{
    Address start;
    Address end;
    BString name;
}
EmbeddedFsFile;

typedef EmbeddedFsFile * EmbeddedFsFilePtr;

OptionalDef(EmbeddedFsFile);

EmbeddedFs EmbeddedFs__new(Address start, Address end);
EmbeddedFsFile EmbeddedFsFile__new(Address start, Address end, BString name);
bool EmbeddedFs__is_valid(EmbeddedFsPtr self);
Optional(EmbeddedFsFile) EmbeddedFs__find(EmbeddedFsPtr self, BString name);
BStringArray EmbeddedFs__list(EmbeddedFsPtr self);

#endif
