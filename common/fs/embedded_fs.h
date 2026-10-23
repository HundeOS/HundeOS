#ifndef HUND_COMMON_EMBEDDED_FS_H
#define HUND_COMMON_EMBEDDED_FS_H

#include <stdint.h>

#include "../types/address.h"
#include "../types/bstring_additional_types.h"
#include "../types/optional.h"

#define EMBEDDED_FS_MAGIC 0x31534645
#define EMBEDDED_FS_VERSION 1
#define EMBEDDED_FS_VADDR 0xa0000000ULL
#define EMBEDDED_FS_MAX_SIZE (128ULL * 1024 * 1024)
#define EMBEDDED_FS_DRIVER "/sbin/drivers/embedfs"
#define EMBEDDED_FS_HOST "/sbin/services/hunde-io-host"

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
} EmbeddedFsHeader;

typedef EmbeddedFsHeader * EmbeddedFsHeaderPtr;

typedef struct EmbeddedFsEntry
{
    uint64_t data_offset;
    uint64_t data_len;
    uint32_t name_offset;
    uint32_t name_len;
    uint32_t flags;
    uint32_t reserved0;
} EmbeddedFsEntry;

typedef EmbeddedFsEntry * EmbeddedFsEntryPtr;

typedef struct EmbeddedFs
{
    Address start;
    Address end;
} EmbeddedFs;

typedef EmbeddedFs * EmbeddedFsPtr;

typedef struct EmbeddedFsFile
{
    Address start;
    Address end;
    BString name;
} EmbeddedFsFile;

typedef EmbeddedFsFile * EmbeddedFsFilePtr;

OptionalDef(EmbeddedFsFile);

EmbeddedFs EmbeddedFs__new(Address start, Address end);
EmbeddedFsFile EmbeddedFsFile__new(Address start, Address end, ConstBStringPtr name);
bool EmbeddedFs__is_valid(EmbeddedFsPtr self);
uint64_t EmbeddedFs__entry_count(EmbeddedFsPtr self);
Optional(EmbeddedFsFile) EmbeddedFs__at(EmbeddedFsPtr self, uint64_t index);
Optional(EmbeddedFsFile) EmbeddedFs__find(EmbeddedFsPtr self, ConstBStringPtr name);
BStringArray EmbeddedFs__list(EmbeddedFsPtr self);

#endif
