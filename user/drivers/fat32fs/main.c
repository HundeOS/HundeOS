#define DRIVER_LOG_TAG "FAT32FS"

#include <string.h>

#include "sys/debug.h"
#include "sys/service.h"
#include "channel/block_client.h"
#include "io/filesystem_driver.h"
#include "types/fixed_array.h"
#include "../../user.h"
#include "../../../common/types/bstring_additional_types.h"

#ifndef FAT32FS_ENTRYPOINT
#define FAT32FS_ENTRYPOINT main
#endif

#define FAT32FS_ARG_COUNT 3
#define FAT32FS_BYTES_PER_SECTOR 512U
#define FAT32FS_SECTORS_PER_CLUSTER 1U
#define FAT32FS_FAT_COUNT 2U
#define FAT32FS_ROOT_ENTRY_SIZE 32U
#define FAT32FS_DIRECTORY_ENTRY_ATTRIBUTES_OFFSET 11U
#define FAT32FS_END_OF_CHAIN 0x0fffffffU
#define FAT32FS_EMPTY_CLUSTER 0x00000000U
#define FAT32FS_ROOT_CLUSTER 2U
#define FAT32FS_ATTR_VOLUME_ID 0x08U
#define FAT32FS_ATTR_LFN 0x0fU
#define FAT32FS_ATTR_DIRECTORY 0x10U
#define FAT32FS_ATTR_ARCHIVE 0x20U
#define FAT32FS_MAX_ROOT_ENTRIES 128U
#define FAT32FS_MAX_FILE_SIZE FAT32FS_BYTES_PER_SECTOR

typedef uint8_t Fat32fsSector[FAT32FS_BYTES_PER_SECTOR];
typedef Fat32fsSector * Fat32fsSectorPtr;

typedef struct Fat32fsSectorData
{
    Fat32fsSector data;
} Fat32fsSectorData;

typedef Fat32fsSectorData Fat32fsDirectorySector;
typedef Fat32fsDirectorySector * Fat32fsDirectorySectorPtr;

ResultDef(Fat32fsSectorData);

typedef struct Fat32fsDirectoryEntry
{
    BString name;
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t size;
} Fat32fsDirectoryEntry;

typedef Fat32fsDirectoryEntry * Fat32fsDirectoryEntryPtr;

OptionalDef(Fat32fsDirectoryEntry);
ResultDef(uint32_t);

static Fat32fsDirectoryEntry Fat32fsDirectoryEntry__new(ConstBStringPtr name, uint8_t attributes,
                                                        uint32_t first_cluster, uint32_t size)
{
    return (Fat32fsDirectoryEntry){
        .name = *name,
        .attributes = attributes,
        .first_cluster = first_cluster,
        .size = size,
    };
}

static bool Fat32fsDirectoryEntry__equals(Fat32fsDirectoryEntryPtr self, Fat32fsDirectoryEntryPtr other)
{
    return BString__equals(&self->name, &other->name) && self->attributes == other->attributes &&
           self->first_cluster == other->first_cluster && self->size == other->size;
}

FixedArrayDefine(Fat32fsDirectoryEntry, FAT32FS_MAX_ROOT_ENTRIES, Fat32fsDirectoryEntries,
                 (EqualFunction)Fat32fsDirectoryEntry__equals);
FixedArrayBodyDefine(Fat32fsDirectoryEntries);
ResultDef(Fat32fsDirectoryEntries);

typedef enum Fat32fsCode
{
    FAT32FS_OK = 0,
    FAT32FS_IO_ERROR = -1,
    FAT32FS_NOT_FOUND = -2,
    FAT32FS_NO_SPACE = -3,
    FAT32FS_INVALID_PATH = -4,
    FAT32FS_UNSUPPORTED_NAME = -5,
    FAT32FS_FILE_TOO_LARGE = -6,
    FAT32FS_NOT_A_DIRECTORY = -7,
    FAT32FS_IS_A_DIRECTORY = -8,
    FAT32FS_ALREADY_EXISTS = -9,
} Fat32fsCode;

ResultDef(Fat32fsCode);

typedef struct Fat32fsDirectoryEntryLocation
{
    uint32_t sector;
    size_t offset;
} Fat32fsDirectoryEntryLocation;

static Fat32fsDirectoryEntryLocation Fat32fsDirectoryEntryLocation__new(uint32_t sector, size_t offset)
{
    return (Fat32fsDirectoryEntryLocation){
        .sector = sector,
        .offset = offset,
    };
}

ResultDef(Fat32fsDirectoryEntryLocation);

typedef struct Fat32fsResolvedParent
{
    uint32_t parent_cluster;
    BString leaf_name;
} Fat32fsResolvedParent;

ResultDef(Fat32fsResolvedParent);

static Fat32fsResolvedParent Fat32fsResolvedParent__new(uint32_t parent_cluster, ConstBStringPtr leaf_name)
{
    return (Fat32fsResolvedParent){
        .parent_cluster = parent_cluster,
        .leaf_name = *leaf_name,
    };
}

typedef struct Fat32fsClusterOffset
{
    uint32_t cluster;
    uint64_t offset;
} Fat32fsClusterOffset;

typedef struct Fat32fsFile
{
    uint32_t first_cluster;
    FilesystemFileSize size;
} Fat32fsFile;

static Fat32fsFile Fat32fsFile__new(uint32_t first_cluster, FilesystemFileSize size)
{
    return (Fat32fsFile){
        .first_cluster = first_cluster,
        .size = size,
    };
}

ResultDef(Fat32fsFile);

ResultDef(Fat32fsClusterOffset);

static Fat32fsClusterOffset Fat32fsClusterOffset__new(uint32_t cluster, uint64_t offset)
{
    return (Fat32fsClusterOffset){
        .cluster = cluster,
        .offset = offset,
    };
}

typedef struct Fat32fsVolume
{
    BString device_path;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint32_t fat_start_sector;
    uint32_t data_start_sector;
    uint32_t root_cluster;
    uint32_t cluster_count;
} Fat32fsVolume;

typedef Fat32fsVolume * Fat32fsVolumePtr;

static BlockClient fat32fs_block_protocol;

static ServiceCode Fat32fs__connect_block_protocol(ConstBStringPtr device_path)
{
    match_result(
        BlockClient__open(device_path),
        success(channel)
        {
            fat32fs_block_protocol = channel;
            return SERVICE_OK;
        },
        error(code, message)
        {
            return (ServiceCode)code;
        }
    );
}

static Fat32fsVolume Fat32fsVolume__new(ConstBStringPtr device_path)
{
    return (Fat32fsVolume){
        .device_path = *device_path,
    };
}

static BString fat32fs_io_error = bstr("FAT32FS IO error");
static BString fat32fs_not_found = bstr("FAT32FS path not found");
static BString fat32fs_no_space = bstr("FAT32FS no space");
static BString fat32fs_invalid_path = bstr("FAT32FS invalid path");
static BString fat32fs_unsupported_name = bstr("FAT32FS unsupported name");
static BString fat32fs_file_too_large = bstr("FAT32FS file too large");
static BString fat32fs_not_a_directory = bstr("FAT32FS not a directory");
static BString fat32fs_is_a_directory = bstr("FAT32FS is a directory");
static BString fat32fs_already_exists = bstr("FAT32FS already exists");

static uint16_t Fat32fs__read_u16(uint8_t * data, size_t offset)
{
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
}

static uint32_t Fat32fs__read_u32(uint8_t * data, size_t offset)
{
    return (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) |
           ((uint32_t)data[offset + 3] << 24);
}

static void Fat32fs__write_u16(uint8_t * data, size_t offset, uint16_t value)
{
    data[offset] = (uint8_t)(value & 0xff);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xff);
}

static void Fat32fs__write_u32(uint8_t * data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t)(value & 0xff);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xff);
    data[offset + 2] = (uint8_t)((value >> 16) & 0xff);
    data[offset + 3] = (uint8_t)((value >> 24) & 0xff);
}

static Result(Fat32fsSectorData) Fat32fs__read_sector(Fat32fsVolumePtr self, uint32_t sector)
{
    Fat32fsSectorData target;
    match_result(
        BlockClient__read(&fat32fs_block_protocol, sector, 1, (Address)&target.data, FAT32FS_BYTES_PER_SECTOR),
        success(result)
        {
            if (result != 1)
            {
                return Error(Fat32fsSectorData, fat32fs_io_error, FAT32FS_IO_ERROR);
            }
        },
        error(code, message)
        {
            return Error(Fat32fsSectorData, message, code);
        }
    );

    return Success(Fat32fsSectorData, target);
}

static Result(Fat32fsCode) Fat32fs__write_sector(Fat32fsVolumePtr self, uint32_t sector, Fat32fsSectorPtr source)
{
    match_result(
        BlockClient__write(&fat32fs_block_protocol, sector, 1, (Address)source, FAT32FS_BYTES_PER_SECTOR),
        success(result)
        {
            if (result != 1)
            {
                return Error(Fat32fsCode, fat32fs_io_error, FAT32FS_IO_ERROR);
            }
        },
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );

    return Success(Fat32fsCode, FAT32FS_OK);
}

static bool Fat32fs__is_end_of_chain(uint32_t value)
{
    return value >= 0x0ffffff8U;
}

static uint32_t Fat32fsVolume__cluster_sector(Fat32fsVolumePtr self, uint32_t cluster)
{
    return self->data_start_sector + ((cluster - 2U) * FAT32FS_SECTORS_PER_CLUSTER);
}

static bool Fat32fsVolume__has_valid_geometry(Fat32fsVolumePtr self)
{
    return self->total_sectors > self->data_start_sector && self->sectors_per_fat > 0 && self->cluster_count > 0;
}

static Result(Fat32fsCode) Fat32fsVolume__mount(Fat32fsVolumePtr self)
{
    Fat32fsSectorData sector;
    match_result(
        Fat32fs__read_sector(self, 0),
        success(data)
        {
            sector = data;
        },
        error(code, message)
        {
            DRIVER_DEBUG("fat32fs: failed to read boot sector\n");
            return Error(Fat32fsCode, message, code);
        }
    );

    if (sector.data[510] != 0x55 || sector.data[511] != 0xaa ||
        Fat32fs__read_u16(sector.data, 11) != FAT32FS_BYTES_PER_SECTOR ||
        sector.data[13] != FAT32FS_SECTORS_PER_CLUSTER || sector.data[16] != FAT32FS_FAT_COUNT ||
        Fat32fs__read_u16(sector.data, 17) != 0 || Fat32fs__read_u16(sector.data, 22) != 0 ||
        Fat32fs__read_u32(sector.data, 36) == 0 || Fat32fs__read_u32(sector.data, 44) < FAT32FS_ROOT_CLUSTER)
    {
        DRIVER_DEBUG("fat32fs: invalid volume\n");
        return Error(Fat32fsCode, fat32fs_invalid_path, FAT32FS_INVALID_PATH);
    }

    self->total_sectors = Fat32fs__read_u32(sector.data, 32);
    self->sectors_per_fat = Fat32fs__read_u32(sector.data, 36);
    self->fat_start_sector = Fat32fs__read_u16(sector.data, 14);
    self->data_start_sector = self->fat_start_sector + (FAT32FS_FAT_COUNT * self->sectors_per_fat);
    self->root_cluster = Fat32fs__read_u32(sector.data, 44);
    self->cluster_count = (self->total_sectors - self->data_start_sector) / FAT32FS_SECTORS_PER_CLUSTER;
    if (!Fat32fsVolume__has_valid_geometry(self))
    {
        return Error(Fat32fsCode, fat32fs_invalid_path, FAT32FS_INVALID_PATH);
    }

    return Success(Fat32fsCode, FAT32FS_OK);
}

static uint32_t Fat32fs__read_directory_cluster(uint8_t * entry)
{
    uint32_t high = Fat32fs__read_u16(entry, 20);
    uint32_t low = Fat32fs__read_u16(entry, 26);
    return (high << 16) | low;
}

static void Fat32fs__write_directory_cluster(uint8_t * entry, uint32_t cluster)
{
    Fat32fs__write_u16(entry, 20, (uint16_t)((cluster >> 16) & 0xffff));
    Fat32fs__write_u16(entry, 26, (uint16_t)(cluster & 0xffff));
}

static uint8_t Fat32fsDirectorySector__entry_attributes(Fat32fsDirectorySectorPtr self, size_t entry_offset)
{
    return self->data[entry_offset + FAT32FS_DIRECTORY_ENTRY_ATTRIBUTES_OFFSET];
}

static void Fat32fsDirectorySector__set_entry_attributes(Fat32fsDirectorySectorPtr self, size_t entry_offset,
                                                         uint8_t attributes)
{
    self->data[entry_offset + FAT32FS_DIRECTORY_ENTRY_ATTRIBUTES_OFFSET] = attributes;
}

static bool Fat32fsDirectorySector__entry_is_directory(Fat32fsDirectorySectorPtr self, size_t entry_offset)
{
    return (Fat32fsDirectorySector__entry_attributes(self, entry_offset) & FAT32FS_ATTR_DIRECTORY) != 0;
}

static bool Fat32fs__is_visible_directory_entry(uint8_t * entry)
{
    uint8_t attributes = entry[FAT32FS_DIRECTORY_ENTRY_ATTRIBUTES_OFFSET];
    return entry[0] != 0xe5 && attributes != FAT32FS_ATTR_LFN && (attributes & FAT32FS_ATTR_VOLUME_ID) == 0;
}

static bool Fat32fs__is_lfn_entry(uint8_t * entry)
{
    return entry[0] != 0xe5 && entry[FAT32FS_DIRECTORY_ENTRY_ATTRIBUTES_OFFSET] == FAT32FS_ATTR_LFN;
}

static BString Fat32fs__decode_83_name(uint8_t * source)
{
    BString result = empty_bstr();
    for (size_t i = 0; i < 8 && source[i] != ' '; i++)
    {
        BString__push_char(&result, (char)source[i]);
    }

    if (source[8] != ' ')
    {
        BString__push_char(&result, '.');
        for (size_t i = 8; i < 11 && source[i] != ' '; i++)
        {
            BString__push_char(&result, (char)source[i]);
        }
    }

    return result;
}

static uint8_t Fat32fs__short_name_checksum(uint8_t * short_name)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < 11; i++)
    {
        sum = (uint8_t)((((sum & 1) << 7) + (sum >> 1) + short_name[i]) & 0xff);
    }
    return sum;
}

static void Fat32fs__clear_lfn(BStringPtr name, uint8_t * checksum, bool * valid)
{
    *name = empty_bstr();
    *checksum = 0;
    *valid = false;
}

static void Fat32fs__write_lfn_char(BStringPtr name, size_t index, uint16_t value)
{
    if (value == 0 || value == 0xffff || index >= BSTRING_MAX_SIZE - 1)
    {
        return;
    }

    BString__set_char_at(name, (StringPos)index, value < 0x80 ? (char)value : '?');
}

static void Fat32fs__accumulate_lfn(uint8_t * entry, BStringPtr name, uint8_t * checksum, bool * valid)
{
    uint8_t sequence = entry[0] & 0x1f;
    if (sequence == 0)
    {
        Fat32fs__clear_lfn(name, checksum, valid);
        return;
    }

    if ((entry[0] & 0x40) != 0)
    {
        *name = empty_bstr();
        *checksum = entry[13];
        *valid = true;
    }
    else if (!*valid || *checksum != entry[13])
    {
        Fat32fs__clear_lfn(name, checksum, valid);
        return;
    }

    size_t base_index = (sequence - 1U) * 13U;
    size_t positions[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    for (size_t i = 0; i < 13; i++)
    {
        Fat32fs__write_lfn_char(name, base_index + i, Fat32fs__read_u16(entry, positions[i]));
    }
}

static BString Fat32fs__entry_name(uint8_t * entry, BStringPtr lfn_name, uint8_t checksum, bool lfn_valid)
{
    if (lfn_valid && checksum == Fat32fs__short_name_checksum(entry) && !BString__is_empty(lfn_name))
    {
        return *lfn_name;
    }

    return Fat32fs__decode_83_name(entry);
}

static bool Fat32fs__names_equal(ConstBStringPtr left, ConstBStringPtr right)
{
    if (BString__length(left) != BString__length(right))
    {
        return false;
    }

    BString uppercase_left = BString__uppercase(left);
    BString uppercase_right = BString__uppercase(right);
    return BString__equals(&uppercase_left, &uppercase_right);
}

static bool Fat32fs__encode_83_name(ConstBStringPtr name, uint8_t target[11])
{
    memset(target, ' ', 11);
    BString uppercase = BString__uppercase(name);
    StringPos dot = -1;
    for (StringPos i = 0; i < BString__length(&uppercase); i++)
    {
        if (BString__char_at_or(&uppercase, i, '\0') == '.')
        {
            dot = i;
            break;
        }
    }

    StringPos base_len = dot < 0 ? BString__length(&uppercase) : dot;
    StringPos ext_len = dot < 0 ? 0 : BString__length(&uppercase) - dot - 1;
    if (base_len <= 0 || base_len > 8 || ext_len > 3)
    {
        return false;
    }

    for (StringPos i = 0; i < base_len; i++)
    {
        char ch = BString__char_at_or(&uppercase, i, '\0');
        if (ch <= ' ' || ch == '.')
        {
            return false;
        }
        target[i] = (uint8_t)ch;
    }

    for (StringPos i = 0; i < ext_len; i++)
    {
        char ch = BString__char_at_or(&uppercase, dot + 1 + i, '\0');
        if (ch <= ' ' || ch == '.')
        {
            return false;
        }
        target[8 + i] = (uint8_t)ch;
    }

    return true;
}

static Result(uint32_t) Fat32fsVolume__read_fat_entry(Fat32fsVolumePtr self, uint32_t cluster)
{
    size_t fat_offset = cluster * sizeof(uint32_t);
    uint32_t relative_sector = fat_offset / FAT32FS_BYTES_PER_SECTOR;
    size_t sector_offset = fat_offset % FAT32FS_BYTES_PER_SECTOR;
    Fat32fsSectorData sector;
    match_result(
        Fat32fs__read_sector(self, self->fat_start_sector + relative_sector),
        success(data)
        {
            sector = data;
        },
        error(code, message)
        {
            return Error(uint32_t, message, code);
        }
    );

    return Success(uint32_t, Fat32fs__read_u32(sector.data, sector_offset) & 0x0fffffffU);
}

static Result(Fat32fsCode) Fat32fsVolume__write_fat_entry(Fat32fsVolumePtr self, uint32_t cluster, uint32_t value)
{
    size_t fat_offset = cluster * sizeof(uint32_t);
    uint32_t relative_sector = fat_offset / FAT32FS_BYTES_PER_SECTOR;
    size_t sector_offset = fat_offset % FAT32FS_BYTES_PER_SECTOR;

    for (uint8_t fat = 0; fat < FAT32FS_FAT_COUNT; fat++)
    {
        uint32_t fat_sector = self->fat_start_sector + (fat * self->sectors_per_fat) + relative_sector;
        Fat32fsSectorData sector;
        match_result(
            Fat32fs__read_sector(self, fat_sector),
            success(data)
            {
                sector = data;
            },
            error(code, message)
            {
                return Error(Fat32fsCode, message, code);
            }
        );
        Fat32fs__write_u32(sector.data, sector_offset, value & 0x0fffffffU);
        match_result(
            Fat32fs__write_sector(self, fat_sector, &sector.data),
            success_do_nothing(),
            error(code, message)
            {
                return Error(Fat32fsCode, message, code);
            }
        );
    }

    return Success(Fat32fsCode, FAT32FS_OK);
}

static Result(uint32_t) Fat32fsVolume__find_free_cluster(Fat32fsVolumePtr self)
{
    for (uint32_t current = 2; current < self->cluster_count + 2; current++)
    {
        match_result(
            Fat32fsVolume__read_fat_entry(self, current),
            success(value)
            {
                if (value == FAT32FS_EMPTY_CLUSTER)
                {
                    return Success(uint32_t, current);
                }
            },
            error(code, message)
            {
                return Error(uint32_t, message, code);
            }
        );
    }

    return Error(uint32_t, fat32fs_no_space, FAT32FS_NO_SPACE);
}

static Result(Fat32fsCode) Fat32fsVolume__free_cluster_chain(Fat32fsVolumePtr self, uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;
    for (uint32_t i = 0; i < self->cluster_count; i++)
    {
        match_result(
            Fat32fsVolume__read_fat_entry(self, cluster),
            success(next_cluster)
            {
                match_result(
                    Fat32fsVolume__write_fat_entry(self, cluster, FAT32FS_EMPTY_CLUSTER),
                    success_do_nothing(),
                    error(code, message)
                    {
                        return Error(Fat32fsCode, message, code);
                    }
                );

                if (Fat32fs__is_end_of_chain(next_cluster) || next_cluster == FAT32FS_EMPTY_CLUSTER)
                {
                    return Success(Fat32fsCode, FAT32FS_OK);
                }

                cluster = next_cluster;
            },
            error(code, message)
            {
                return Error(Fat32fsCode, message, code);
            }
        );
    }

    return Error(Fat32fsCode, fat32fs_not_found, FAT32FS_NOT_FOUND);
}

static Result(Fat32fsDirectoryEntryLocation)
    Fat32fsVolume__find_directory_entry(Fat32fsVolumePtr self, uint32_t directory_cluster, ConstBStringPtr name)
{
    uint32_t directory_sector_start = Fat32fsVolume__cluster_sector(self, directory_cluster);
    BString lfn_name = empty_bstr();
    uint8_t lfn_checksum = 0;
    bool lfn_valid = false;

    for (uint8_t relative_sector = 0; relative_sector < FAT32FS_SECTORS_PER_CLUSTER; relative_sector++)
    {
        Fat32fsSectorData directory_sector;
        match_result(
            Fat32fs__read_sector(self, directory_sector_start + relative_sector),
            success(data)
            {
                directory_sector = data;
            },
            error(code, message)
            {
                return Error(Fat32fsDirectoryEntryLocation, message, code);
            }
        );

        for (size_t entry_offset = 0; entry_offset < FAT32FS_BYTES_PER_SECTOR; entry_offset += FAT32FS_ROOT_ENTRY_SIZE)
        {
            if (directory_sector.data[entry_offset] == 0x00)
            {
                return Error(Fat32fsDirectoryEntryLocation, fat32fs_not_found, FAT32FS_NOT_FOUND);
            }
            if (directory_sector.data[entry_offset] == 0xe5)
            {
                Fat32fs__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }
            if (Fat32fs__is_lfn_entry(&directory_sector.data[entry_offset]))
            {
                Fat32fs__accumulate_lfn(&directory_sector.data[entry_offset], &lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }
            if (!Fat32fs__is_visible_directory_entry(&directory_sector.data[entry_offset]))
            {
                Fat32fs__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }

            BString entry_name =
                Fat32fs__entry_name(&directory_sector.data[entry_offset], &lfn_name, lfn_checksum, lfn_valid);
            if (Fat32fs__names_equal(&entry_name, name))
            {
                Fat32fsDirectoryEntryLocation location =
                    Fat32fsDirectoryEntryLocation__new(directory_sector_start + relative_sector, entry_offset);
                return Success(Fat32fsDirectoryEntryLocation, location);
            }
            Fat32fs__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
        }
    }

    return Error(Fat32fsDirectoryEntryLocation, fat32fs_not_found, FAT32FS_NOT_FOUND);
}

static Result(Fat32fsDirectoryEntryLocation)
    Fat32fsVolume__find_free_directory_entry(Fat32fsVolumePtr self, uint32_t directory_cluster)
{
    uint32_t directory_sector_start = Fat32fsVolume__cluster_sector(self, directory_cluster);

    for (uint8_t relative_sector = 0; relative_sector < FAT32FS_SECTORS_PER_CLUSTER; relative_sector++)
    {
        Fat32fsSectorData directory_sector;
        match_result(
            Fat32fs__read_sector(self, directory_sector_start + relative_sector),
            success(data)
            {
                directory_sector = data;
            },
            error(code, message)
            {
                return Error(Fat32fsDirectoryEntryLocation, message, code);
            }
        );

        for (size_t entry_offset = 0; entry_offset < FAT32FS_BYTES_PER_SECTOR; entry_offset += FAT32FS_ROOT_ENTRY_SIZE)
        {
            if (directory_sector.data[entry_offset] == 0x00 || directory_sector.data[entry_offset] == 0xe5)
            {
                Fat32fsDirectoryEntryLocation location =
                    Fat32fsDirectoryEntryLocation__new(directory_sector_start + relative_sector, entry_offset);
                return Success(Fat32fsDirectoryEntryLocation, location);
            }
        }
    }

    return Error(Fat32fsDirectoryEntryLocation, fat32fs_no_space, FAT32FS_NO_SPACE);
}

static Result(Fat32fsDirectoryEntries) Fat32fsVolume__list_directory(Fat32fsVolumePtr self, uint32_t directory_cluster)
{
    Fat32fsDirectoryEntries entries = Fat32fsDirectoryEntries__new();
    uint32_t directory_sector = Fat32fsVolume__cluster_sector(self, directory_cluster);
    BString lfn_name = empty_bstr();
    uint8_t lfn_checksum = 0;
    bool lfn_valid = false;

    for (uint8_t relative_sector = 0; relative_sector < FAT32FS_SECTORS_PER_CLUSTER; relative_sector++)
    {
        Fat32fsSectorData sector;
        match_result(
            Fat32fs__read_sector(self, directory_sector + relative_sector),
            success(data)
            {
                sector = data;
            },
            error(code, message)
            {
                return Error(Fat32fsDirectoryEntries, message, code);
            }
        );

        for (size_t offset = 0; offset < FAT32FS_BYTES_PER_SECTOR; offset += FAT32FS_ROOT_ENTRY_SIZE)
        {
            if (sector.data[offset] == 0x00)
            {
                return Success(Fat32fsDirectoryEntries, entries);
            }
            if (sector.data[offset] == 0xe5)
            {
                Fat32fs__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }
            if (Fat32fs__is_lfn_entry(&sector.data[offset]))
            {
                Fat32fs__accumulate_lfn(&sector.data[offset], &lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }
            if (!Fat32fs__is_visible_directory_entry(&sector.data[offset]))
            {
                Fat32fs__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }

            BString entry_name = Fat32fs__entry_name(&sector.data[offset], &lfn_name, lfn_checksum, lfn_valid);
            Fat32fsDirectoryEntry entry = Fat32fsDirectoryEntry__new(
                &entry_name, Fat32fsDirectorySector__entry_attributes(&sector, offset),
                Fat32fs__read_directory_cluster(&sector.data[offset]), Fat32fs__read_u32(sector.data, offset + 28));
            match_result(
                Fat32fsDirectoryEntries__push(&entries, entry),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(Fat32fsDirectoryEntries, message, code);
                }
            );
            Fat32fs__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
        }
    }

    return Success(Fat32fsDirectoryEntries, entries);
}

static Result(Fat32fsDirectoryEntryLocation) Fat32fsVolume__entry_at_path(Fat32fsVolumePtr self, ConstBStringPtr path);

static Result(Fat32fsResolvedParent) Fat32fsVolume__resolve_parent(Fat32fsVolumePtr self, ConstBStringPtr path)
{
    BStringArray segments = BString__split(path, '/');
    size_t segment_count = BStringArray__len(&segments);
    if (segment_count == 0)
    {
        return Error(Fat32fsResolvedParent, fat32fs_invalid_path, FAT32FS_INVALID_PATH);
    }

    BString empty_name = empty_bstr();
    Fat32fsResolvedParent resolved = Fat32fsResolvedParent__new(self->root_cluster, &empty_name);
    match_optional(
        BStringArray__at(&segments, segment_count - 1),
        some(segment)
        {
            resolved.leaf_name = segment;
        },
        none()
        {
            return Error(Fat32fsResolvedParent, fat32fs_invalid_path, FAT32FS_INVALID_PATH);
        }
    );

    for (size_t i = 0; i + 1 < segment_count; i++)
    {
        BString segment = empty_bstr();
        match_optional(
            BStringArray__at(&segments, i),
            some(path_segment)
            {
                segment = path_segment;
            },
            none()
            {
                return Error(Fat32fsResolvedParent, fat32fs_invalid_path, FAT32FS_INVALID_PATH);
            }
        );

        Fat32fsDirectoryEntryLocation location;
        match_result(
            Fat32fsVolume__find_directory_entry(self, resolved.parent_cluster, &segment),
            success(found)
            {
                location = found;
            },
            error(code, message)
            {
                return Error(Fat32fsResolvedParent, message, code);
            }
        );

        Fat32fsSectorData directory_sector;
        match_result(
            Fat32fs__read_sector(self, location.sector),
            success(data)
            {
                directory_sector = data;
            },
            error(code, message)
            {
                return Error(Fat32fsResolvedParent, message, code);
            }
        );
        uint32_t next_cluster = Fat32fs__read_directory_cluster(&directory_sector.data[location.offset]);
        if (!Fat32fsDirectorySector__entry_is_directory(&directory_sector, location.offset) || next_cluster < 2)
        {
            return Error(Fat32fsResolvedParent, fat32fs_not_a_directory, FAT32FS_NOT_A_DIRECTORY);
        }
        resolved.parent_cluster = next_cluster;
    }

    if (BString__is_empty(&resolved.leaf_name))
    {
        return Error(Fat32fsResolvedParent, fat32fs_invalid_path, FAT32FS_INVALID_PATH);
    }

    return Success(Fat32fsResolvedParent, resolved);
}

static Result(Fat32fsDirectoryEntryLocation) Fat32fsVolume__entry_at_path(Fat32fsVolumePtr self, ConstBStringPtr path)
{
    match_result(
        Fat32fsVolume__resolve_parent(self, path),
        success(resolved)
        {
            return Fat32fsVolume__find_directory_entry(self, resolved.parent_cluster, &resolved.leaf_name);
        },
        error(code, message)
        {
            return Error(Fat32fsDirectoryEntryLocation, message, code);
        }
    );
}

static Result(Fat32fsClusterOffset)
    Fat32fsVolume__cluster_for_offset(Fat32fsVolumePtr self, uint32_t first_cluster, uint64_t offset)
{
    uint32_t hops = (uint32_t)(offset / FAT32FS_BYTES_PER_SECTOR);
    uint32_t current = first_cluster;
    for (uint32_t i = 0; i < hops; i++)
    {
        match_result(
            Fat32fsVolume__read_fat_entry(self, current),
            success(next)
            {
                if (Fat32fs__is_end_of_chain(next) || next == FAT32FS_EMPTY_CLUSTER)
                {
                    return Error(Fat32fsClusterOffset, fat32fs_not_found, FAT32FS_NOT_FOUND);
                }
                current = next;
            },
            error(code, message)
            {
                return Error(Fat32fsClusterOffset, message, code);
            }
        );
    }

    Fat32fsClusterOffset result = Fat32fsClusterOffset__new(current, offset % FAT32FS_BYTES_PER_SECTOR);
    return Success(Fat32fsClusterOffset, result);
}

static Result(uint32_t)
    Fat32fsVolume__create_file_at_path(Fat32fsVolumePtr self, ConstBStringPtr path, uint8_t * data, uint32_t size)
{
    if (size > FAT32FS_MAX_FILE_SIZE)
    {
        return Error(uint32_t, fat32fs_file_too_large, FAT32FS_FILE_TOO_LARGE);
    }

    Fat32fsResolvedParent resolved;
    match_result(
        Fat32fsVolume__resolve_parent(self, path),
        success(value)
        {
            resolved = value;
        },
        error(code, message)
        {
            return Error(uint32_t, message, code);
        }
    );

    uint8_t encoded_name[11];
    if (!Fat32fs__encode_83_name(&resolved.leaf_name, encoded_name))
    {
        return Error(uint32_t, fat32fs_unsupported_name, FAT32FS_UNSUPPORTED_NAME);
    }

    uint32_t first_cluster = 0;
    Fat32fsDirectoryEntryLocation entry_location = Fat32fsDirectoryEntryLocation__new(0, 0);
    match_result(
        Fat32fsVolume__find_directory_entry(self, resolved.parent_cluster, &resolved.leaf_name),
        success(existing_location)
        {
            entry_location = existing_location;
            Fat32fsSectorData existing;
            match_result(
                Fat32fs__read_sector(self, entry_location.sector),
                success(data)
                {
                    existing = data;
                },
                error(code, message)
                {
                    return Error(uint32_t, message, code);
                }
            );
            uint32_t old_cluster = Fat32fs__read_directory_cluster(&existing.data[entry_location.offset]);
            if (Fat32fsDirectorySector__entry_is_directory(&existing, entry_location.offset) || old_cluster < 2)
            {
                return Error(uint32_t, fat32fs_is_a_directory, FAT32FS_IS_A_DIRECTORY);
            }
            match_result(
                Fat32fsVolume__free_cluster_chain(self, old_cluster),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(uint32_t, message, code);
                }
            );
            first_cluster = old_cluster;
        },
        error(code, message)
        {
            if (code != FAT32FS_NOT_FOUND)
            {
                return Error(uint32_t, message, code);
            }
        }
    );

    if (first_cluster == 0)
    {
        match_result(
            Fat32fsVolume__find_free_cluster(self),
            success(cluster)
            {
                first_cluster = cluster;
            },
            error(code, message)
            {
                return Error(uint32_t, message, code);
            }
        );
    }

    match_result(
        Fat32fsVolume__write_fat_entry(self, first_cluster, FAT32FS_END_OF_CHAIN),
        success_do_nothing(),
        error(code, message)
        {
            return Error(uint32_t, message, code);
        }
    );

    Fat32fsSector file_sector;
    memset(file_sector, 0, sizeof(file_sector));
    if (size > 0)
    {
        memcpy(file_sector, data, size);
    }
    match_result(
        Fat32fs__write_sector(self, Fat32fsVolume__cluster_sector(self, first_cluster), &file_sector),
        success_do_nothing(),
        error(code, message)
        {
            return Error(uint32_t, message, code);
        }
    );

    if (entry_location.sector == 0)
    {
        match_result(
            Fat32fsVolume__find_free_directory_entry(self, resolved.parent_cluster),
            success(location)
            {
                entry_location = location;
            },
            error(code, message)
            {
                return Error(uint32_t, message, code);
            }
        );
    }

    Fat32fsSectorData directory_sector;
    match_result(
        Fat32fs__read_sector(self, entry_location.sector),
        success(data)
        {
            directory_sector = data;
        },
        error(code, message)
        {
            return Error(uint32_t, message, code);
        }
    );

    memcpy(&directory_sector.data[entry_location.offset], encoded_name, 11);
    Fat32fsDirectorySector__set_entry_attributes(&directory_sector, entry_location.offset, FAT32FS_ATTR_ARCHIVE);
    Fat32fs__write_directory_cluster(&directory_sector.data[entry_location.offset], first_cluster);
    Fat32fs__write_u32(directory_sector.data, entry_location.offset + 28, size);
    match_result(
        Fat32fs__write_sector(self, entry_location.sector, &directory_sector.data),
        success_do_nothing(),
        error(code, message)
        {
            return Error(uint32_t, message, code);
        }
    );

    return Success(uint32_t, first_cluster);
}

static Result(Fat32fsCode) Fat32fsVolume__create_directory_at_path(Fat32fsVolumePtr self, ConstBStringPtr path)
{
    Fat32fsResolvedParent resolved;
    match_result(
        Fat32fsVolume__resolve_parent(self, path),
        success(value)
        {
            resolved = value;
        },
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );

    uint8_t encoded_name[11];
    if (!Fat32fs__encode_83_name(&resolved.leaf_name, encoded_name))
    {
        return Error(Fat32fsCode, fat32fs_unsupported_name, FAT32FS_UNSUPPORTED_NAME);
    }

    match_result(
        Fat32fsVolume__find_directory_entry(self, resolved.parent_cluster, &resolved.leaf_name),
        success(existing)
        {
            return Error(Fat32fsCode, fat32fs_already_exists, FAT32FS_ALREADY_EXISTS);
        },
        error(code, message)
        {
            if (code != FAT32FS_NOT_FOUND)
            {
                return Error(Fat32fsCode, message, code);
            }
        }
    );

    uint32_t cluster;
    match_result(
        Fat32fsVolume__find_free_cluster(self),
        success(found_cluster)
        {
            cluster = found_cluster;
        },
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );
    match_result(
        Fat32fsVolume__write_fat_entry(self, cluster, FAT32FS_END_OF_CHAIN),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );
    Fat32fsDirectoryEntryLocation entry_location;
    match_result(
        Fat32fsVolume__find_free_directory_entry(self, resolved.parent_cluster),
        success(location)
        {
            entry_location = location;
        },
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );

    Fat32fsSector empty_directory;
    memset(empty_directory, 0, sizeof(empty_directory));
    match_result(
        Fat32fs__write_sector(self, Fat32fsVolume__cluster_sector(self, cluster), &empty_directory),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );

    Fat32fsSectorData directory_sector;
    match_result(
        Fat32fs__read_sector(self, entry_location.sector),
        success(data)
        {
            directory_sector = data;
        },
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );

    memcpy(&directory_sector.data[entry_location.offset], encoded_name, 11);
    Fat32fsDirectorySector__set_entry_attributes(&directory_sector, entry_location.offset, FAT32FS_ATTR_DIRECTORY);
    Fat32fs__write_directory_cluster(&directory_sector.data[entry_location.offset], cluster);
    Fat32fs__write_u32(directory_sector.data, entry_location.offset + 28, 0);
    return Fat32fs__write_sector(self, entry_location.sector, &directory_sector.data);
}

static Result(Fat32fsCode) Fat32fsVolume__remove_entry_in_directory(Fat32fsVolumePtr self, uint32_t directory_cluster,
                                                                    ConstBStringPtr name, Flags flags);

static Result(Fat32fsCode) Fat32fsVolume__remove_directory_children(Fat32fsVolumePtr self, uint32_t directory_cluster)
{
    Fat32fsDirectoryEntries entries;
    match_result(
        Fat32fsVolume__list_directory(self, directory_cluster),
        success(result)
        {
            entries = result;
        },
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );

    for_each_fa(
        Fat32fsDirectoryEntries,
        entries,
        index,
        entry,
        {
            match_result(
                Fat32fsVolume__remove_entry_in_directory(self, directory_cluster, &entry.name, IO_REMOVE_RECURSIVE),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(Fat32fsCode, message, code);
                }
            );
        }
    );

    return Success(Fat32fsCode, FAT32FS_OK);
}

static Result(Fat32fsCode) Fat32fsVolume__remove_entry_in_directory(Fat32fsVolumePtr self, uint32_t directory_cluster,
                                                                    ConstBStringPtr name, Flags flags)
{
    Fat32fsDirectoryEntryLocation entry_location;
    match_result(
        Fat32fsVolume__find_directory_entry(self, directory_cluster, name),
        success(location)
        {
            entry_location = location;
        },
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );

    Fat32fsSectorData directory_sector;
    match_result(
        Fat32fs__read_sector(self, entry_location.sector),
        success(data)
        {
            directory_sector = data;
        },
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );

    uint32_t first_cluster = Fat32fs__read_directory_cluster(&directory_sector.data[entry_location.offset]);
    if (first_cluster < 2)
    {
        return Error(Fat32fsCode, fat32fs_not_found, FAT32FS_NOT_FOUND);
    }

    if (Fat32fsDirectorySector__entry_is_directory(&directory_sector, entry_location.offset))
    {
        Fat32fsDirectoryEntries entries;
        match_result(
            Fat32fsVolume__list_directory(self, first_cluster),
            success(result)
            {
                entries = result;
            },
            error(code, message)
            {
                return Error(Fat32fsCode, message, code);
            }
        );
        if (Fat32fsDirectoryEntries__len(&entries) > 0 && (flags & IO_REMOVE_RECURSIVE) == 0)
        {
            return Error(Fat32fsCode, fat32fs_is_a_directory, FAT32FS_IS_A_DIRECTORY);
        }
        if ((flags & IO_REMOVE_RECURSIVE) != 0)
        {
            match_result(
                Fat32fsVolume__remove_directory_children(self, first_cluster),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(Fat32fsCode, message, code);
                }
            );
        }
    }

    directory_sector.data[entry_location.offset] = 0xe5;
    match_result(
        Fat32fs__write_sector(self, entry_location.sector, &directory_sector.data),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );
    return Fat32fsVolume__free_cluster_chain(self, first_cluster);
}

static Result(Fat32fsCode) Fat32fsVolume__remove_at_path(Fat32fsVolumePtr self, ConstBStringPtr path, Flags flags)
{
    match_result(
        Fat32fsVolume__resolve_parent(self, path),
        success(resolved)
        {
            return Fat32fsVolume__remove_entry_in_directory(self, resolved.parent_cluster, &resolved.leaf_name, flags);
        },
        error(code, message)
        {
            return Error(Fat32fsCode, message, code);
        }
    );
}

static DirectoryEntry Fat32fs__directory_entry(Fat32fsDirectoryEntryPtr entry)
{
    FileMode mode =
        (entry->attributes & FAT32FS_ATTR_DIRECTORY) != 0 ? IO_MODE_TYPE_DIR | 0755 : IO_MODE_TYPE_FILE | 0644;
    return DirectoryEntry__new(IO_DIRENT_ATTR_NONE, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT, mode, &entry->name);
}

static DirectoryEntryCount Fat32fsVolume__list_dir_from(Fat32fsVolumePtr self, ConstBStringPtr path,
                                                        DirectoryEntryPtr entries, size_t max_entries, uint64_t skip)
{
    uint32_t cluster = self->root_cluster;
    if (!BString__equals(path, &bstr("/")) && BString__length(path) > 0)
    {
        Fat32fsDirectoryEntryLocation location;
        Fat32fsSectorData sector;
        match_result(
            Fat32fsVolume__entry_at_path(self, path),
            success(entry_location)
            {
                location = entry_location;
            },
            error(code, message)
            {
                return IO_INVALID_PATH;
            }
        );
        match_result(
            Fat32fs__read_sector(self, location.sector),
            success(data)
            {
                sector = data;
            },
            error(code, message)
            {
                return IO_INVALID_PATH;
            }
        );
        if (!Fat32fsDirectorySector__entry_is_directory(&sector, location.offset))
        {
            return IO_INVALID_PATH;
        }
        cluster = Fat32fs__read_directory_cluster(&sector.data[location.offset]);
    }

    Fat32fsDirectoryEntries source;
    match_result(
        Fat32fsVolume__list_directory(self, cluster),
        success(entries_result)
        {
            source = entries_result;
        },
        error(code, message)
        {
            return IO_INVALID_PATH;
        }
    );

    DirectoryEntryCount count = 0;
    for_each_fa(
        Fat32fsDirectoryEntries,
        source,
        index,
        entry,
        {
            if (skip > 0)
            {
                skip--;
                continue;
            }
            if ((size_t)count < max_entries)
            {
                entries[count] = Fat32fs__directory_entry(&entry);
                count++;
            }
        }
    );

    return count;
}

static Result(Fat32fsFile) Fat32fsVolume__file_at_path(Fat32fsVolumePtr self, ConstBStringPtr path)
{
    Fat32fsDirectoryEntryLocation location;
    match_result(
        Fat32fsVolume__entry_at_path(self, path),
        success(value)
        {
            location = value;
        },
        error(code, message)
        {
            return Error(Fat32fsFile, message, IO_INVALID_PATH);
        }
    );
    Fat32fsSectorData entry;
    match_result(
        Fat32fs__read_sector(self, location.sector),
        success(value)
        {
            entry = value;
        },
        error(code, message)
        {
            return Error(Fat32fsFile, message, IO_INVALID_PATH);
        }
    );
    if (Fat32fsDirectorySector__entry_is_directory(&entry, location.offset))
    {
        return Error(Fat32fsFile, fat32fs_is_a_directory, IO_INVALID_PATH);
    }
    return Success(Fat32fsFile, Fat32fsFile__new(Fat32fs__read_directory_cluster(&entry.data[location.offset]),
                                                 Fat32fs__read_u32(entry.data, location.offset + 28)));
}

static Result(FilesystemFileInfo) Fat32fs__describe(IOServiceContext context, ConstBStringPtr path)
{
    Fat32fsVolumePtr volume = context;
    if (BString__equals(path, &bstr("/")))
    {
        return Success(FilesystemFileInfo, FilesystemFileInfo__new(0, IO_MODE_TYPE_DIR | 0755));
    }

    Fat32fsDirectoryEntryLocation location;
    match_result(
        Fat32fsVolume__entry_at_path(volume, path),
        success(value)
        {
            location = value;
        },
        error(code, message)
        {
            return Error(FilesystemFileInfo, message, IO_INVALID_PATH);
        }
    );
    match_result(
        Fat32fs__read_sector(volume, location.sector),
        success(sector)
        {
            FileMode mode = Fat32fsDirectorySector__entry_is_directory(&sector, location.offset)
                                ? IO_MODE_TYPE_DIR | 0755
                                : IO_MODE_TYPE_FILE | 0755;
            FilesystemFileSize size = Fat32fs__read_u32(sector.data, location.offset + 28);
            return Success(FilesystemFileInfo, FilesystemFileInfo__new(size, mode));
        },
        error(code, message)
        {
            return Error(FilesystemFileInfo, message, IO_INVALID_PATH);
        }
    );
}

static Result(FilesystemFileSize)
    Fat32fs__read(IOServiceContext context, ConstBStringPtr path, FilesystemFileSize offset, FilesystemFileDataPtr data,
                  FilesystemFileSize capacity)
{
    Fat32fsVolumePtr volume = context;
    Fat32fsFile file;
    match_result(
        Fat32fsVolume__file_at_path(volume, path),
        success(value)
        {
            file = value;
        },
        error(code, message)
        {
            return Error(FilesystemFileSize, message, code);
        }
    );
    if (offset >= file.size || capacity == 0)
    {
        return Success(FilesystemFileSize, 0);
    }

    FilesystemFileSize remaining = file.size - offset;
    if (remaining > capacity)
    {
        remaining = capacity;
    }
    FilesystemFileSize total = 0;
    while (total < remaining)
    {
        Fat32fsClusterOffset cluster;
        match_result(
            Fat32fsVolume__cluster_for_offset(volume, file.first_cluster, offset + total),
            success(value)
            {
                cluster = value;
            },
            error(code, message)
            {
                return Error(FilesystemFileSize, message, IO_INVALID_PATH);
            }
        );
        match_result(
            Fat32fs__read_sector(volume, Fat32fsVolume__cluster_sector(volume, cluster.cluster)),
            success(sector)
            {
                FilesystemFileSize count = FAT32FS_BYTES_PER_SECTOR - cluster.offset;
                if (count > remaining - total)
                {
                    count = remaining - total;
                }
                memcpy(data + total, sector.data + cluster.offset, count);
                total += count;
            },
            error(code, message)
            {
                return Error(FilesystemFileSize, message, IO_INVALID_PATH);
            }
        );
    }

    return Success(FilesystemFileSize, total);
}

static IOStatus Fat32fs__store(IOServiceContext context, ConstBStringPtr path, FilesystemFileDataPtr data,
                               FilesystemFileSize size)
{
    Fat32fsVolumePtr volume = context;
    match_result(
        Fat32fsVolume__create_file_at_path(volume, path, data, size),
        success(value)
        {
            return IO_OK;
        },
        error(code, message)
        {
            return IO_INVALID_PATH;
        }
    );
}

static Result(DirectoryEntryCount) Fat32fs__list(IOServiceContext context, ConstBStringPtr path,
                                                 DirectoryEntryPtr entries, size_t capacity, uint64_t offset)
{
    DirectoryEntryCount count = Fat32fsVolume__list_dir_from(context, path, entries, capacity, offset);
    if (count < 0)
    {
        return Error(DirectoryEntryCount, bstr("Directory lookup failed"), count);
    }
    return Success(DirectoryEntryCount, count);
}

static IOStatus Fat32fs__mkdir(IOServiceContext context, ConstBStringPtr path)
{
    Fat32fsVolumePtr volume = context;
    match_result(
        Fat32fsVolume__create_directory_at_path(volume, path),
        success(value)
        {
            return IO_OK;
        },
        error(code, message)
        {
            return IO_INVALID_PATH;
        }
    );
}

static IOStatus Fat32fs__remove(IOServiceContext context, ConstBStringPtr path, Flags flags)
{
    Fat32fsVolumePtr volume = context;
    match_result(
        Fat32fsVolume__remove_at_path(volume, path, flags),
        success(value)
        {
            return IO_OK;
        },
        error(code, message)
        {
            return IO_INVALID_PATH;
        }
    );
}

static FilesystemOperations Fat32fs__operations(void)
{
    return FilesystemOperations__new_stream(Fat32fs__describe, Fat32fs__read, Fat32fs__store, Fat32fs__list,
                                            Fat32fs__mkdir, Fat32fs__remove, FilesystemOperations__unsupported_link,
                                            FAT32FS_MAX_FILE_SIZE);
}

int FAT32FS_ENTRYPOINT(int argc, char ** argv)
{
    if (argc != FAT32FS_ARG_COUNT || !service_ok(service_become_service()))
    {
        return 1;
    }
    BString device = BString__new(argv[1]);
    BString mountpoint = BString__new(argv[2]);
    static Fat32fsVolume volume;
    volume = Fat32fsVolume__new(&device);
    if (!service_ok(Fat32fs__connect_block_protocol(&device)))
    {
        return 1;
    }

    match_result(
        Fat32fsVolume__mount(&volume),
        success_do_nothing(),
        error(code, message)
        {
            DRIVER_DEBUG("fat32fs: mount failed: %B (%i)\n", &message, (int)code);
            BlockClient__close(&fat32fs_block_protocol);
            return 1;
        }
    );
    static FilesystemDriver driver;
    FilesystemOperations operations = Fat32fs__operations();
    driver = FilesystemDriver__new(&volume, &operations);
    int result = FilesystemDriver__run(&driver, &mountpoint);
    BlockClient__close(&fat32fs_block_protocol);
    return result;
}
