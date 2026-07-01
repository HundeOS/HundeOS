#include "fat32.h"
#include "../../../../kalloc.h"
#include "../../../../debug.h"

FixedArrayBodyDefine(Fat32DirectoryEntries);
MapBodyDefine(Fat32FileHandleMap);

static KString invalid_argument = KSTRING_NEW("Invalid FAT32 argument");
static KString io_error = KSTRING_NEW("FAT32 block IO failed");
static KString invalid_volume = KSTRING_NEW("Invalid FAT32 volume");
static KString no_space = KSTRING_NEW("FAT32 volume has no free space");
static KString not_found = KSTRING_NEW("FAT32 file not found");
static KString already_exists = KSTRING_NEW("FAT32 file already exists");
static KString unsupported_name = KSTRING_NEW("FAT32 only supports 8.3 root names");
static KString file_too_large = KSTRING_NEW("FAT32 file is too large for this operation");
static KString invalid_path = KSTRING_NEW("Invalid FAT32 path");
static KString invalid_handle = KSTRING_NEW("Invalid FAT32 handle");
static KString not_implemented = KSTRING_NEW("FAT32 operation not implemented");
static KString directory_not_empty = KSTRING_NEW("FAT32 directory not empty");

#define FAT32_DEFAULT_MEMORY_SECTORS 131072

bool Fat32DirectoryEntry__equals(Fat32DirectoryEntryPtr self, Fat32DirectoryEntryPtr other)
{
    return KString__equals(&self->name, &other->name)
        && self->attributes == other->attributes
        && self->first_cluster == other->first_cluster
        && self->size == other->size;
}

static bool Fat32__is_visible_directory_entry(DataPtr entry)
{
    uint8_t attributes = entry[11];
    return entry[0] != 0xe5
        && attributes != FAT32_ATTR_LFN
        && (attributes & FAT32_ATTR_VOLUME_ID) == 0;
}

static bool Fat32__is_lfn_entry(DataPtr entry)
{
    return entry[0] != 0xe5 && entry[11] == FAT32_ATTR_LFN;
}

Fat32Volume Fat32Volume__new(Optional(BlockDevicePtr) device, BlockCount total_sectors)
{
    Fat32Volume volume = {
        .device = device,
        .total_sectors = total_sectors,
        .sectors_per_fat = 0,
        .fat_start_sector = FAT32_RESERVED_SECTORS,
        .data_start_sector = 0,
        .root_cluster = FAT32_ROOT_CLUSTER,
        .cluster_count = 0,
    };

    if (total_sectors > FAT32_RESERVED_SECTORS)
    {
        uint32_t sectors_per_fat = 1;
        while (true)
        {
            BlockCount available_data_sectors =
                total_sectors - FAT32_RESERVED_SECTORS - (FAT32_FAT_COUNT * sectors_per_fat);
            BlockCount clusters = available_data_sectors / FAT32_SECTORS_PER_CLUSTER;
            BlockCount required_fat_bytes = (clusters + 2) * sizeof(uint32_t);
            BlockCount required_fat_sectors =
                (required_fat_bytes + FAT32_BYTES_PER_SECTOR - 1) / FAT32_BYTES_PER_SECTOR;

            if (required_fat_sectors <= sectors_per_fat)
            {
                volume.sectors_per_fat = sectors_per_fat;
                volume.data_start_sector = FAT32_RESERVED_SECTORS + (FAT32_FAT_COUNT * sectors_per_fat);
                volume.cluster_count = (uint32_t) clusters;
                break;
            }

            sectors_per_fat = (uint32_t) required_fat_sectors;
        }
    }

    return volume;
}

static bool Fat32Volume__has_valid_geometry(Fat32VolumePtr self)
{
    Match(BlockDevicePtr, self->device)
        IsSome(device,
        {
            return self->total_sectors > self->data_start_sector
                && self->sectors_per_fat > 0
                && self->cluster_count > 0;
        })
    End

    return false;
}

static void Fat32__zero(DataPtr data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        data[i] = 0;
    }
}

static void Fat32__copy(DataPtr target, DataPtr source, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        target[i] = source[i];
    }
}

static size_t Fat32__min_size(size_t left, size_t right)
{
    if (left < right)
    {
        return left;
    }

    return right;
}

static uint16_t Fat32__read_u16(DataPtr data, size_t offset)
{
    return (uint16_t) data[offset] | ((uint16_t) data[offset + 1] << 8);
}

static uint32_t Fat32__read_u32(DataPtr data, size_t offset)
{
    return (uint32_t) data[offset]
        | ((uint32_t) data[offset + 1] << 8)
        | ((uint32_t) data[offset + 2] << 16)
        | ((uint32_t) data[offset + 3] << 24);
}

static void Fat32__write_u16(DataPtr data, size_t offset, uint16_t value)
{
    data[offset] = (Data) (value & 0xff);
    data[offset + 1] = (Data) ((value >> 8) & 0xff);
}

static void Fat32__write_u32(DataPtr data, size_t offset, uint32_t value)
{
    data[offset] = (Data) (value & 0xff);
    data[offset + 1] = (Data) ((value >> 8) & 0xff);
    data[offset + 2] = (Data) ((value >> 16) & 0xff);
    data[offset + 3] = (Data) ((value >> 24) & 0xff);
}

static Result(Fat32Code) Fat32Volume__read_sector(Fat32VolumePtr self, BlockNumber sector, DataPtr target)
{
    Match(BlockDevicePtr, self->device)
        IsSome(device,
        {
            ResultMatch(BlockCount, BlockDevice__read(device, sector, 1, target))
                IsSuccess(count, {
                    if (count == 1)
                    {
                        return Success(Fat32Code, FAT32_OK);
                    }
                })
                IsError(code, message, return Error(Fat32Code, io_error, FAT32_IO_ERROR))
            End
        })
    End

    return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
}

static Result(Fat32Code) Fat32Volume__write_sector(Fat32VolumePtr self, BlockNumber sector, DataPtr source)
{
    Match(BlockDevicePtr, self->device)
        IsSome(device,
        {
            ResultMatch(BlockCount, BlockDevice__write(device, sector, 1, source))
                IsSuccess(count, {
                    if (count == 1)
                    {
                        return Success(Fat32Code, FAT32_OK);
                    }
                })
                IsError(code, message, return Error(Fat32Code, io_error, FAT32_IO_ERROR))
            End
        })
    End

    return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
}

static char Fat32__uppercase(char ch)
{
    if (ch >= 'a' && ch <= 'z')
    {
        return (char) (ch - ('a' - 'A'));
    }

    return ch;
}

static bool Fat32__is_valid_name_char(char ch)
{
    return (ch >= 'A' && ch <= 'Z')
        || (ch >= '0' && ch <= '9')
        || ch == '_'
        || ch == '-';
}

static Result(Fat32Code) Fat32__encode_83_name(KStringPtr name, Data target[11])
{
    if (name->len == 0)
    {
        return Error(Fat32Code, unsupported_name, FAT32_UNSUPPORTED_NAME);
    }

    for (size_t i = 0; i < 11; i++)
    {
        target[i] = ' ';
    }

    size_t target_index = 0;
    bool extension = false;
    bool saw_dot = false;

    for (uint64_t i = 0; i < name->len; i++)
    {
        char ch = Fat32__uppercase(name->data[i]);

        if (ch == '.')
        {
            if (saw_dot || i == 0)
            {
                return Error(Fat32Code, unsupported_name, FAT32_UNSUPPORTED_NAME);
            }
            saw_dot = true;
            extension = true;
            target_index = 8;
            continue;
        }

        if (!Fat32__is_valid_name_char(ch))
        {
            return Error(Fat32Code, unsupported_name, FAT32_UNSUPPORTED_NAME);
        }

        if ((!extension && target_index >= 8) || (extension && target_index >= 11))
        {
            return Error(Fat32Code, unsupported_name, FAT32_UNSUPPORTED_NAME);
        }

        target[target_index] = (Data) ch;
        target_index++;
    }

    if (saw_dot && target[8] == ' ')
    {
        return Error(Fat32Code, unsupported_name, FAT32_UNSUPPORTED_NAME);
    }

    return Success(Fat32Code, FAT32_OK);
}

static KString Fat32__decode_83_name(DataPtr source)
{
    KString result = EMPTY_KSTRING;

    for (size_t i = 0; i < 8 && source[i] != ' '; i++)
    {
        result.data[result.len] = (char) source[i];
        result.len++;
    }

    if (source[8] != ' ')
    {
        result.data[result.len] = '.';
        result.len++;

        for (size_t i = 8; i < 11 && source[i] != ' '; i++)
        {
            result.data[result.len] = (char) source[i];
            result.len++;
        }
    }

    result.data[result.len] = 0;
    return result;
}

static uint8_t Fat32__short_name_checksum(DataPtr short_name)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < 11; i++)
    {
        sum = (uint8_t) ((((sum & 1) << 7) + (sum >> 1) + short_name[i]) & 0xff);
    }
    return sum;
}

static void Fat32__clear_lfn(KStringPtr name, uint8_t * checksum, bool * valid)
{
    *name = EMPTY_KSTRING;
    *checksum = 0;
    *valid = false;
}

static void Fat32__write_lfn_char(KStringPtr name, size_t index, uint16_t value)
{
    if (value == 0 || value == 0xffff || index >= MAX_SIZE - 1)
    {
        return;
    }

    name->data[index] = value < 0x80 ? (char) value : '?';
    if (index >= name->len)
    {
        name->len = index + 1;
        name->data[name->len] = 0;
    }
}

static void Fat32__accumulate_lfn(DataPtr entry, KStringPtr name, uint8_t * checksum, bool * valid)
{
    uint8_t sequence = entry[0] & 0x1f;
    if (sequence == 0)
    {
        Fat32__clear_lfn(name, checksum, valid);
        return;
    }

    if ((entry[0] & 0x40) != 0)
    {
        *name = EMPTY_KSTRING;
        *checksum = entry[13];
        *valid = true;
    }
    else if (!*valid || *checksum != entry[13])
    {
        Fat32__clear_lfn(name, checksum, valid);
        return;
    }

    size_t base_index = (sequence - 1) * 13;
    size_t positions[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    for (size_t i = 0; i < 13; i++)
    {
        Fat32__write_lfn_char(name, base_index + i, Fat32__read_u16(entry, positions[i]));
    }
}

static KString Fat32__entry_name(DataPtr entry, KStringPtr lfn_name, uint8_t checksum, bool lfn_valid)
{
    if (lfn_valid && checksum == Fat32__short_name_checksum(entry) && lfn_name->len > 0)
    {
        return *lfn_name;
    }

    return Fat32__decode_83_name(entry);
}

static bool Fat32__chars_equal_ignore_case(char left, char right)
{
    return Fat32__uppercase(left) == Fat32__uppercase(right);
}

static bool Fat32__names_equal(ConstKStringPtr left, ConstKStringPtr right)
{
    if (left->len != right->len)
    {
        return false;
    }

    for (size_t i = 0; i < left->len; i++)
    {
        if (!Fat32__chars_equal_ignore_case(left->data[i], right->data[i]))
        {
            return false;
        }
    }

    return true;
}

static Result(Fat32Code) Fat32Volume__write_fat_entry(Fat32VolumePtr self, uint32_t cluster, uint32_t value)
{
    Data sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    size_t fat_offset = cluster * sizeof(uint32_t);
    BlockNumber relative_sector = fat_offset / FAT32_BYTES_PER_SECTOR;
    size_t sector_offset = fat_offset % FAT32_BYTES_PER_SECTOR;

    for (uint8_t fat = 0; fat < FAT32_FAT_COUNT; fat++)
    {
        BlockNumber fat_sector = self->fat_start_sector + (fat * self->sectors_per_fat) + relative_sector;

        ResultMatch(Fat32Code, Fat32Volume__read_sector(self, fat_sector, sector))
            IsError(code, message, return Error(Fat32Code, message, code))
        End

        Fat32__write_u32(sector, sector_offset, value & 0x0fffffff);

        ResultMatch(Fat32Code, Fat32Volume__write_sector(self, fat_sector, sector))
            IsError(code, message, return Error(Fat32Code, message, code))
        End
    }

    return Success(Fat32Code, FAT32_OK);
}

static Result(Fat32Code) Fat32Volume__read_fat_entry(Fat32VolumePtr self, uint32_t cluster, uint32_t * value)
{
    Data sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    size_t fat_offset = cluster * sizeof(uint32_t);
    BlockNumber relative_sector = fat_offset / FAT32_BYTES_PER_SECTOR;
    size_t sector_offset = fat_offset % FAT32_BYTES_PER_SECTOR;

    ResultMatch(Fat32Code, Fat32Volume__read_sector(self, self->fat_start_sector + relative_sector, sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    *value = Fat32__read_u32(sector, sector_offset) & 0x0fffffff;
    return Success(Fat32Code, FAT32_OK);
}

static bool Fat32__is_end_of_chain(uint32_t value)
{
    return value >= 0x0ffffff8;
}

static Result(Fat32Code) Fat32Volume__free_cluster_chain(Fat32VolumePtr self, uint32_t first_cluster)
{
    if (!Fat32Volume__has_valid_geometry(self) || first_cluster < 2)
    {
        return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    uint32_t cluster = first_cluster;
    for (uint32_t i = 0; i < self->cluster_count; i++)
    {
        uint32_t next_cluster = FAT32_END_OF_CHAIN;
        ResultMatch(Fat32Code, Fat32Volume__read_fat_entry(self, cluster, &next_cluster))
            IsError(code, message, return Error(Fat32Code, message, code))
        End

        ResultMatch(Fat32Code, Fat32Volume__write_fat_entry(self, cluster, FAT32_EMPTY_CLUSTER))
            IsError(code, message, return Error(Fat32Code, message, code))
        End

        if (Fat32__is_end_of_chain(next_cluster) || next_cluster == FAT32_EMPTY_CLUSTER)
        {
            return Success(Fat32Code, FAT32_OK);
        }

        if (next_cluster < 2)
        {
            return Error(Fat32Code, invalid_volume, FAT32_INVALID_VOLUME);
        }

        cluster = next_cluster;
    }

    return Error(Fat32Code, invalid_volume, FAT32_INVALID_VOLUME);
}

static Result(Fat32Code) Fat32Volume__find_free_cluster(Fat32VolumePtr self, uint32_t * cluster)
{
    for (uint32_t current = 2; current < self->cluster_count + 2; current++)
    {
        uint32_t value = FAT32_END_OF_CHAIN;
        ResultMatch(Fat32Code, Fat32Volume__read_fat_entry(self, current, &value))
            IsError(code, message, return Error(Fat32Code, message, code))
        End

        if (value == FAT32_EMPTY_CLUSTER)
        {
            *cluster = current;
            return Success(Fat32Code, FAT32_OK);
        }
    }

    return Error(Fat32Code, no_space, FAT32_NO_SPACE);
}

static BlockNumber Fat32Volume__cluster_sector(Fat32VolumePtr self, uint32_t cluster)
{
    return self->data_start_sector + ((cluster - 2) * FAT32_SECTORS_PER_CLUSTER);
}

static uint32_t Fat32__read_directory_cluster(DataPtr entry)
{
    uint32_t high = Fat32__read_u16(entry, 20);
    uint32_t low = Fat32__read_u16(entry, 26);
    return (high << 16) | low;
}

static void Fat32__write_directory_cluster(DataPtr entry, uint32_t cluster)
{
    Fat32__write_u16(entry, 20, (uint16_t) ((cluster >> 16) & 0xffff));
    Fat32__write_u16(entry, 26, (uint16_t) (cluster & 0xffff));
}

static Result(Fat32Code) Fat32Volume__find_directory_entry(
    Fat32VolumePtr self, uint32_t directory_cluster, KString name, BlockNumber * sector, size_t * offset
);

static Result(Fat32Code) Fat32Volume__find_free_directory_entry(
    Fat32VolumePtr self, uint32_t directory_cluster, BlockNumber * sector, size_t * offset
);

static Result(Fat32DirectoryEntries) Fat32Volume__list_directory(
    Fat32VolumePtr self, uint32_t directory_cluster
);

static Result(Fat32Code) Fat32Volume__find_directory_entry(
    Fat32VolumePtr self, uint32_t directory_cluster, KString name, BlockNumber * sector, size_t * offset
)
{
    Data directory_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    BlockNumber directory_sector_start = Fat32Volume__cluster_sector(self, directory_cluster);
    KString lfn_name = EMPTY_KSTRING;
    uint8_t lfn_checksum = 0;
    bool lfn_valid = false;

    for (uint8_t relative_sector = 0; relative_sector < FAT32_SECTORS_PER_CLUSTER; relative_sector++)
    {
        ResultMatch(Fat32Code, Fat32Volume__read_sector(
            self,
            directory_sector_start + relative_sector,
            directory_sector
        ))
            IsError(code, message, return Error(Fat32Code, message, code))
        End

        for (size_t entry_offset = 0; entry_offset < FAT32_BYTES_PER_SECTOR; entry_offset += FAT32_ROOT_ENTRY_SIZE)
        {
            if (directory_sector[entry_offset] == 0x00)
            {
                return Error(Fat32Code, not_found, FAT32_NOT_FOUND);
            }

            if (directory_sector[entry_offset] == 0xe5)
            {
                Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }

            if (Fat32__is_lfn_entry(&directory_sector[entry_offset]))
            {
                Fat32__accumulate_lfn(&directory_sector[entry_offset], &lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }

            if (!Fat32__is_visible_directory_entry(&directory_sector[entry_offset]))
            {
                Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }

            KString entry_name = Fat32__entry_name(
                &directory_sector[entry_offset],
                &lfn_name,
                lfn_checksum,
                lfn_valid
            );
            if (Fat32__names_equal(&entry_name, &name))
            {
                *sector = directory_sector_start + relative_sector;
                *offset = entry_offset;
                return Success(Fat32Code, FAT32_OK);
            }

            Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
        }
    }

    return Error(Fat32Code, not_found, FAT32_NOT_FOUND);
}

static Result(Fat32Code) Fat32Volume__find_free_directory_entry(
    Fat32VolumePtr self, uint32_t directory_cluster, BlockNumber * sector, size_t * offset
)
{
    Data directory_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    BlockNumber directory_sector_start = Fat32Volume__cluster_sector(self, directory_cluster);

    for (uint8_t relative_sector = 0; relative_sector < FAT32_SECTORS_PER_CLUSTER; relative_sector++)
    {
        ResultMatch(Fat32Code, Fat32Volume__read_sector(
            self,
            directory_sector_start + relative_sector,
            directory_sector
        ))
            IsError(code, message, return Error(Fat32Code, message, code))
        End

        for (size_t entry_offset = 0; entry_offset < FAT32_BYTES_PER_SECTOR; entry_offset += FAT32_ROOT_ENTRY_SIZE)
        {
            if (directory_sector[entry_offset] == 0x00 || directory_sector[entry_offset] == 0xe5)
            {
                *sector = directory_sector_start + relative_sector;
                *offset = entry_offset;
                return Success(Fat32Code, FAT32_OK);
            }
        }
    }

    return Error(Fat32Code, no_space, FAT32_NO_SPACE);
}

Result(Fat32Code) Fat32Volume__format(Fat32VolumePtr self)
{
    if (!Fat32Volume__has_valid_geometry(self))
    {
        return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    Data sector[FAT32_BYTES_PER_SECTOR] = { 0 };

    sector[0] = 0xeb;
    sector[1] = 0x3c;
    sector[2] = 0x90;
    sector[3] = 'M';
    sector[4] = 'I';
    sector[5] = 'L';
    sector[6] = 'L';
    sector[7] = 'I';
    sector[8] = 'K';
    sector[9] = 'R';
    sector[10] = 'N';
    Fat32__write_u16(sector, 11, FAT32_BYTES_PER_SECTOR);
    sector[13] = FAT32_SECTORS_PER_CLUSTER;
    Fat32__write_u16(sector, 14, FAT32_RESERVED_SECTORS);
    sector[16] = FAT32_FAT_COUNT;
    Fat32__write_u16(sector, 17, 0);
    Fat32__write_u16(sector, 19, 0);
    sector[21] = FAT32_MEDIA_FIXED_DISK;
    Fat32__write_u16(sector, 22, 0);
    Fat32__write_u16(sector, 24, 1);
    Fat32__write_u16(sector, 26, 1);
    Fat32__write_u32(sector, 32, (uint32_t) self->total_sectors);
    Fat32__write_u32(sector, 36, self->sectors_per_fat);
    Fat32__write_u16(sector, 40, 0);
    Fat32__write_u16(sector, 42, 0);
    Fat32__write_u32(sector, 44, self->root_cluster);
    Fat32__write_u16(sector, 48, 1);
    Fat32__write_u16(sector, 50, 6);
    sector[64] = 0x80;
    sector[66] = 0x29;
    Fat32__write_u32(sector, 67, 0x46415432);
    sector[71] = 'M';
    sector[72] = 'I';
    sector[73] = 'L';
    sector[74] = 'L';
    sector[75] = 'I';
    sector[76] = 'K';
    sector[77] = 'R';
    sector[78] = 'N';
    sector[79] = ' ';
    sector[80] = ' ';
    sector[81] = ' ';
    sector[82] = 'F';
    sector[83] = 'A';
    sector[84] = 'T';
    sector[85] = '3';
    sector[86] = '2';
    sector[87] = ' ';
    sector[88] = ' ';
    sector[89] = ' ';
    sector[510] = 0x55;
    sector[511] = 0xaa;

    ResultMatch(Fat32Code, Fat32Volume__write_sector(self, 0, sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    Fat32__zero(sector, FAT32_BYTES_PER_SECTOR);
    Fat32__write_u32(sector, 0, 0x41615252);
    Fat32__write_u32(sector, 484, 0x61417272);
    Fat32__write_u32(sector, 488, 0xffffffff);
    Fat32__write_u32(sector, 492, 0xffffffff);
    sector[510] = 0x55;
    sector[511] = 0xaa;
    ResultMatch(Fat32Code, Fat32Volume__write_sector(self, 1, sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    for (uint8_t fat = 0; fat < FAT32_FAT_COUNT; fat++)
    {
        for (uint32_t i = 0; i < self->sectors_per_fat; i++)
        {
            Fat32__zero(sector, FAT32_BYTES_PER_SECTOR);
            if (i == 0)
            {
                Fat32__write_u32(sector, 0, 0x0fffff00 | FAT32_MEDIA_FIXED_DISK);
                Fat32__write_u32(sector, 4, FAT32_END_OF_CHAIN);
                Fat32__write_u32(sector, 8, FAT32_END_OF_CHAIN);
            }

            ResultMatch(Fat32Code, Fat32Volume__write_sector(
                self,
                self->fat_start_sector + (fat * self->sectors_per_fat) + i,
                sector
            ))
                IsError(code, message, return Error(Fat32Code, message, code))
            End
        }
    }

    Fat32__zero(sector, FAT32_BYTES_PER_SECTOR);
    ResultMatch(Fat32Code, Fat32Volume__write_sector(
        self,
        Fat32Volume__cluster_sector(self, self->root_cluster),
        sector
    ))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    return Success(Fat32Code, FAT32_OK);
}

static Result(Fat32Code) Fat32Volume__mount_from_sector(Fat32VolumePtr self, DataPtr sector)
{
    Match(BlockDevicePtr, self->device)
        IsNone(return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT))
    End

    if (sector[510] != 0x55
        || sector[511] != 0xaa
        || Fat32__read_u16(sector, 11) != FAT32_BYTES_PER_SECTOR
        || sector[13] != FAT32_SECTORS_PER_CLUSTER
        || sector[16] != FAT32_FAT_COUNT
        || Fat32__read_u16(sector, 17) != 0
        || Fat32__read_u16(sector, 22) != 0
        || Fat32__read_u32(sector, 36) == 0
        || Fat32__read_u32(sector, 44) < FAT32_ROOT_CLUSTER)
    {
        return Error(Fat32Code, invalid_volume, FAT32_INVALID_VOLUME);
    }

    BlockCount total_sectors = Fat32__read_u32(sector, 32);

    self->total_sectors = total_sectors;
    self->sectors_per_fat = Fat32__read_u32(sector, 36);
    self->fat_start_sector = Fat32__read_u16(sector, 14);
    self->data_start_sector = self->fat_start_sector + (FAT32_FAT_COUNT * self->sectors_per_fat);
    self->root_cluster = Fat32__read_u32(sector, 44);
    self->cluster_count = (uint32_t) ((self->total_sectors - self->data_start_sector) / FAT32_SECTORS_PER_CLUSTER);

    if (!Fat32Volume__has_valid_geometry(self))
    {
        return Error(Fat32Code, invalid_volume, FAT32_INVALID_VOLUME);
    }

    return Success(Fat32Code, FAT32_OK);
}

Result(Fat32Code) Fat32Volume__mount(Fat32VolumePtr self)
{
    Data sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    ResultMatch(Fat32Code, Fat32Volume__read_sector(self, 0, sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    return Fat32Volume__mount_from_sector(self, sector);
}

Result(Fat32DirectoryEntries) Fat32Volume__list_root(Fat32VolumePtr self)
{
    return Fat32Volume__list_directory(self, self->root_cluster);
}

static Result(Fat32DirectoryEntries) Fat32Volume__list_directory(Fat32VolumePtr self, uint32_t directory_cluster)
{
    if (!Fat32Volume__has_valid_geometry(self))
    {
        return Error(Fat32DirectoryEntries, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    Fat32DirectoryEntries entries = Fat32DirectoryEntries__new();
    Data sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    BlockNumber directory_sector = Fat32Volume__cluster_sector(self, directory_cluster);
    KString lfn_name = EMPTY_KSTRING;
    uint8_t lfn_checksum = 0;
    bool lfn_valid = false;

    for (uint8_t relative_sector = 0; relative_sector < FAT32_SECTORS_PER_CLUSTER; relative_sector++)
    {
        ResultMatch(Fat32Code, Fat32Volume__read_sector(self, directory_sector + relative_sector, sector))
            IsError(code, message, return Error(Fat32DirectoryEntries, message, code))
        End

        for (size_t offset = 0; offset < FAT32_BYTES_PER_SECTOR; offset += FAT32_ROOT_ENTRY_SIZE)
        {
            if (sector[offset] == 0x00)
            {
                return Success(Fat32DirectoryEntries, entries);
            }

            if (sector[offset] == 0xe5)
            {
                Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }

            if (Fat32__is_lfn_entry(&sector[offset]))
            {
                Fat32__accumulate_lfn(&sector[offset], &lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }

            if (!Fat32__is_visible_directory_entry(&sector[offset]))
            {
                Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
                continue;
            }

            Fat32DirectoryEntry entry = {
                .name = Fat32__entry_name(&sector[offset], &lfn_name, lfn_checksum, lfn_valid),
                .attributes = sector[offset + 11],
                .first_cluster = Fat32__read_directory_cluster(&sector[offset]),
                .size = Fat32__read_u32(sector, offset + 28),
            };

            ResultMatch(CollectionCode, Fat32DirectoryEntries__push(&entries, entry))
                IsError(code, message, return Error(Fat32DirectoryEntries, message, code))
            End
            Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
        }
    }

    return Success(Fat32DirectoryEntries, entries);
}

static Result(Fat32Code) Fat32Volume__entry_cluster_and_attributes(
    Fat32VolumePtr self, BlockNumber sector, size_t offset, uint32_t * cluster, uint8_t * attributes
)
{
    Data directory_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    ResultMatch(Fat32Code, Fat32Volume__read_sector(self, sector, directory_sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    *cluster = Fat32__read_directory_cluster(&directory_sector[offset]);
    *attributes = directory_sector[offset + 11];
    return Success(Fat32Code, FAT32_OK);
}

static Result(Fat32Code) Fat32Volume__resolve_parent(
    Fat32VolumePtr self, KString path, uint32_t * parent_cluster, KString * leaf_name
)
{
    if (!Fat32Volume__has_valid_geometry(self))
    {
        return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    KStringArray segments = KString__split(&path, '/');
    size_t segment_count = KStringArray__len(&segments);
    if (segment_count == 0)
    {
        return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    *parent_cluster = self->root_cluster;
    *leaf_name = Unwrap(KString, KStringArray__at(&segments, segment_count - 1), EMPTY_KSTRING);

    for (size_t i = 0; i < segment_count - 1; i++)
    {
        KString segment = Unwrap(KString, KStringArray__at(&segments, i), EMPTY_KSTRING);
        BlockNumber entry_sector = 0;
        size_t entry_offset = 0;
        ResultMatch(Fat32Code, Fat32Volume__find_directory_entry(
            self,
            *parent_cluster,
            segment,
            &entry_sector,
            &entry_offset
        ))
            IsError(code, message, return Error(Fat32Code, message, code))
        End

        uint32_t next_cluster = 0;
        uint8_t attributes = 0;
        ResultMatch(Fat32Code, Fat32Volume__entry_cluster_and_attributes(
            self,
            entry_sector,
            entry_offset,
            &next_cluster,
            &attributes
        ))
            IsError(code, message, return Error(Fat32Code, message, code))
        End

        if ((attributes & FAT32_ATTR_DIRECTORY) == 0 || next_cluster < 2)
        {
            return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
        }

        *parent_cluster = next_cluster;
    }

    return Success(Fat32Code, FAT32_OK);
}

static Result(Fat32Code) Fat32Volume__create_file_in_directory(
    Fat32VolumePtr self, uint32_t directory_cluster, KString name, DataPtr data, uint32_t size
)
{
    if (!Fat32Volume__has_valid_geometry(self))
    {
        return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    if (size > FAT32_MAX_FILE_SIZE)
    {
        return Error(Fat32Code, file_too_large, FAT32_FILE_TOO_LARGE);
    }

    Data encoded_name[11] = { 0 };
    ResultMatch(Fat32Code, Fat32__encode_83_name(&name, encoded_name))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    BlockNumber existing_sector = 0;
    size_t existing_offset = 0;
    ResultMatch(Fat32Code, Fat32Volume__find_directory_entry(
        self,
        directory_cluster,
        name,
        &existing_sector,
        &existing_offset
    ))
        IsSuccess(_, return Error(Fat32Code, already_exists, FAT32_ALREADY_EXISTS))
        IsError(code, message, {
            if (code != FAT32_NOT_FOUND)
            {
                return Error(Fat32Code, message, code);
            }
        })
    End

    uint32_t cluster = 0;
    ResultMatch(Fat32Code, Fat32Volume__find_free_cluster(self, &cluster))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    ResultMatch(Fat32Code, Fat32Volume__write_fat_entry(self, cluster, FAT32_END_OF_CHAIN))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    Data file_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    if (size > 0)
    {
        Fat32__copy(file_sector, data, size);
    }

    ResultMatch(Fat32Code, Fat32Volume__write_sector(self, Fat32Volume__cluster_sector(self, cluster), file_sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    BlockNumber entry_sector = 0;
    size_t entry_offset = 0;
    ResultMatch(Fat32Code, Fat32Volume__find_free_directory_entry(
        self,
        directory_cluster,
        &entry_sector,
        &entry_offset
    ))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    Data directory_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    ResultMatch(Fat32Code, Fat32Volume__read_sector(self, entry_sector, directory_sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    Fat32__copy(&directory_sector[entry_offset], encoded_name, 11);
    directory_sector[entry_offset + 11] = FAT32_ATTR_ARCHIVE;
    Fat32__write_directory_cluster(&directory_sector[entry_offset], cluster);
    Fat32__write_u32(directory_sector, entry_offset + 28, size);

    ResultMatch(Fat32Code, Fat32Volume__write_sector(self, entry_sector, directory_sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    return Success(Fat32Code, FAT32_OK);
}

static Result(Fat32Code) Fat32Volume__create_file_at_path(
    Fat32VolumePtr self, KString path, DataPtr data, uint32_t size
)
{
    uint32_t parent_cluster = 0;
    KString leaf_name = EMPTY_KSTRING;
    ResultMatch(Fat32Code, Fat32Volume__resolve_parent(self, path, &parent_cluster, &leaf_name))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    return Fat32Volume__create_file_in_directory(self, parent_cluster, leaf_name, data, size);
}

Result(Fat32Code) Fat32Volume__create_root_file(
    Fat32VolumePtr self, KString name, DataPtr data, uint32_t size
)
{
    return Fat32Volume__create_file_in_directory(self, self->root_cluster, name, data, size);
}

static Result(Fat32Code) Fat32Volume__create_directory_in_directory(
    Fat32VolumePtr self, uint32_t directory_cluster, KString name
)
{
    if (!Fat32Volume__has_valid_geometry(self))
    {
        return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    Data encoded_name[11] = { 0 };
    ResultMatch(Fat32Code, Fat32__encode_83_name(&name, encoded_name))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    BlockNumber existing_sector = 0;
    size_t existing_offset = 0;
    ResultMatch(Fat32Code, Fat32Volume__find_directory_entry(
        self,
        directory_cluster,
        name,
        &existing_sector,
        &existing_offset
    ))
        IsSuccess(_, return Error(Fat32Code, already_exists, FAT32_ALREADY_EXISTS))
        IsError(code, message, {
            if (code != FAT32_NOT_FOUND)
            {
                return Error(Fat32Code, message, code);
            }
        })
    End

    uint32_t cluster = 0;
    ResultMatch(Fat32Code, Fat32Volume__find_free_cluster(self, &cluster))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    ResultMatch(Fat32Code, Fat32Volume__write_fat_entry(self, cluster, FAT32_END_OF_CHAIN))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    Data directory_data[FAT32_BYTES_PER_SECTOR] = { 0 };
    ResultMatch(Fat32Code, Fat32Volume__write_sector(self, Fat32Volume__cluster_sector(self, cluster), directory_data))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    BlockNumber entry_sector = 0;
    size_t entry_offset = 0;
    ResultMatch(Fat32Code, Fat32Volume__find_free_directory_entry(
        self,
        directory_cluster,
        &entry_sector,
        &entry_offset
    ))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    Data directory_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    ResultMatch(Fat32Code, Fat32Volume__read_sector(self, entry_sector, directory_sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    Fat32__copy(&directory_sector[entry_offset], encoded_name, 11);
    directory_sector[entry_offset + 11] = FAT32_ATTR_DIRECTORY;
    Fat32__write_directory_cluster(&directory_sector[entry_offset], cluster);
    Fat32__write_u32(directory_sector, entry_offset + 28, 0);

    ResultMatch(Fat32Code, Fat32Volume__write_sector(self, entry_sector, directory_sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    return Success(Fat32Code, FAT32_OK);
}

static Result(Fat32Code) Fat32Volume__create_directory_at_path(Fat32VolumePtr self, KString path)
{
    uint32_t parent_cluster = 0;
    KString leaf_name = EMPTY_KSTRING;
    ResultMatch(Fat32Code, Fat32Volume__resolve_parent(self, path, &parent_cluster, &leaf_name))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    return Fat32Volume__create_directory_in_directory(self, parent_cluster, leaf_name);
}

Result(Fat32Code) Fat32Volume__create_root_directory(Fat32VolumePtr self, KString name)
{
    return Fat32Volume__create_directory_in_directory(self, self->root_cluster, name);
}

static Result(Fat32Code) Fat32Volume__read_file_chain(
    Fat32VolumePtr self,
    uint32_t cluster,
    DataPtr target,
    uint32_t file_size,
    uint32_t target_size,
    uint32_t * bytes_read
)
{
    if (cluster < 2 || file_size > target_size || file_size > FAT32_MAX_READ_SIZE)
    {
        return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    uint32_t remaining = file_size;
    uint32_t copied = 0;
    uint32_t current_cluster = cluster;
    Data file_sector[FAT32_BYTES_PER_SECTOR] = { 0 };

    for (uint32_t chain_index = 0; chain_index < self->cluster_count && remaining > 0; chain_index++)
    {
        if (current_cluster < 2 || current_cluster >= self->cluster_count + 2)
        {
            return Error(Fat32Code, invalid_volume, FAT32_INVALID_VOLUME);
        }

        ResultMatch(Fat32Code, Fat32Volume__read_sector(
            self,
            Fat32Volume__cluster_sector(self, current_cluster),
            file_sector
        ))
            IsError(code, message, return Error(Fat32Code, message, code))
        End

        uint32_t copy_size = (uint32_t) Fat32__min_size(remaining, FAT32_BYTES_PER_SECTOR);
        Fat32__copy(&target[copied], file_sector, copy_size);
        copied += copy_size;
        remaining -= copy_size;

        if (remaining == 0)
        {
            *bytes_read = copied;
            return Success(Fat32Code, FAT32_OK);
        }

        uint32_t next_cluster = FAT32_END_OF_CHAIN;
        ResultMatch(Fat32Code, Fat32Volume__read_fat_entry(self, current_cluster, &next_cluster))
            IsError(code, message, return Error(Fat32Code, message, code))
        End

        if (Fat32__is_end_of_chain(next_cluster) || next_cluster == FAT32_EMPTY_CLUSTER)
        {
            return Error(Fat32Code, invalid_volume, FAT32_INVALID_VOLUME);
        }

        current_cluster = next_cluster;
    }

    if (remaining == 0)
    {
        *bytes_read = copied;
        return Success(Fat32Code, FAT32_OK);
    }

    return Error(Fat32Code, invalid_volume, FAT32_INVALID_VOLUME);
}

static Result(Fat32Code) Fat32Volume__find_entry_at_path(
    Fat32VolumePtr self, KString path, BlockNumber * sector, size_t * offset
)
{
    uint32_t parent_cluster = 0;
    KString leaf_name = EMPTY_KSTRING;
    ResultMatch(Fat32Code, Fat32Volume__resolve_parent(self, path, &parent_cluster, &leaf_name))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    return Fat32Volume__find_directory_entry(self, parent_cluster, leaf_name, sector, offset);
}

static Result(Fat32Code) Fat32Volume__mark_directory_entry_deleted(
    Fat32VolumePtr self, BlockNumber sector, size_t offset
)
{
    Data directory_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    ResultMatch(Fat32Code, Fat32Volume__read_sector(self, sector, directory_sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    directory_sector[offset] = 0xe5;

    ResultMatch(Fat32Code, Fat32Volume__write_sector(self, sector, directory_sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    return Success(Fat32Code, FAT32_OK);
}

static Result(Fat32Code) Fat32Volume__remove_directory_children(Fat32VolumePtr self, uint32_t directory_cluster);

static Result(Fat32Code) Fat32Volume__remove_entry_in_directory(
    Fat32VolumePtr self, uint32_t directory_cluster, KString name, Flags flags
)
{
    BlockNumber entry_sector = 0;
    size_t entry_offset = 0;
    ResultMatch(Fat32Code, Fat32Volume__find_directory_entry(
        self,
        directory_cluster,
        name,
        &entry_sector,
        &entry_offset
    ))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    Data directory_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    ResultMatch(Fat32Code, Fat32Volume__read_sector(self, entry_sector, directory_sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    uint8_t attributes = directory_sector[entry_offset + 11];
    uint32_t first_cluster = Fat32__read_directory_cluster(&directory_sector[entry_offset]);
    if (first_cluster < 2)
    {
        return Error(Fat32Code, invalid_volume, FAT32_INVALID_VOLUME);
    }

    if ((attributes & FAT32_ATTR_DIRECTORY) != 0)
    {
        ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_directory(self, first_cluster))
            IsSuccess(entries,
            {
                if (entries.len > 0 && (flags & IO_REMOVE_RECURSIVE) == 0)
                {
                    return Error(Fat32Code, directory_not_empty, FAT32_ALREADY_EXISTS);
                }
            })
            IsError(code, message, return Error(Fat32Code, message, code))
        End

        if ((flags & IO_REMOVE_RECURSIVE) != 0)
        {
            ResultMatch(Fat32Code, Fat32Volume__remove_directory_children(self, first_cluster))
                IsError(code, message, return Error(Fat32Code, message, code))
            End
        }
    }

    ResultMatch(Fat32Code, Fat32Volume__mark_directory_entry_deleted(self, entry_sector, entry_offset))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    return Fat32Volume__free_cluster_chain(self, first_cluster);
}

static Result(Fat32Code) Fat32Volume__remove_directory_children(Fat32VolumePtr self, uint32_t directory_cluster)
{
    ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_directory(self, directory_cluster))
        IsSuccess(entries,
        {
            FAIterate(Fat32DirectoryEntries, entries, index, entry)
                ResultMatch(Fat32Code, Fat32Volume__remove_entry_in_directory(
                    self,
                    directory_cluster,
                    entry.name,
                    IO_REMOVE_RECURSIVE
                ))
                    IsError(code, message, return Error(Fat32Code, message, code))
                End
            End
            return Success(Fat32Code, FAT32_OK);
        })
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    return Success(Fat32Code, FAT32_OK);
}

static Result(Fat32Code) Fat32Volume__remove_at_path(Fat32VolumePtr self, KString path, Flags flags)
{
    uint32_t parent_cluster = 0;
    KString leaf_name = EMPTY_KSTRING;
    ResultMatch(Fat32Code, Fat32Volume__resolve_parent(self, path, &parent_cluster, &leaf_name))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    return Fat32Volume__remove_entry_in_directory(self, parent_cluster, leaf_name, flags);
}

Result(Fat32Code) Fat32Volume__read_root_file(
    Fat32VolumePtr self, KString name, DataPtr target, uint32_t target_size, uint32_t * bytes_read
)
{
    if (!Fat32Volume__has_valid_geometry(self))
    {
        return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    BlockNumber entry_sector = 0;
    size_t entry_offset = 0;
    ResultMatch(Fat32Code, Fat32Volume__find_entry_at_path(self, name, &entry_sector, &entry_offset))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    Data directory_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    ResultMatch(Fat32Code, Fat32Volume__read_sector(self, entry_sector, directory_sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    uint32_t cluster = Fat32__read_directory_cluster(&directory_sector[entry_offset]);
    uint32_t size = Fat32__read_u32(directory_sector, entry_offset + 28);
    uint8_t attributes = directory_sector[entry_offset + 11];
    if ((attributes & FAT32_ATTR_DIRECTORY) != 0 || size > target_size || size > FAT32_MAX_READ_SIZE || cluster < 2)
    {
        return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    return Fat32Volume__read_file_chain(self, cluster, target, size, target_size, bytes_read);
}

static IOStatus Fat32FilesystemIO__status_from_fat32(Fat32Code code)
{
    if (code == FAT32_NOT_FOUND || code == FAT32_UNSUPPORTED_NAME || code == FAT32_INVALID_ARGUMENT)
    {
        return IO_INVALID_PATH;
    }

    if (code == FAT32_IO_ERROR || code == FAT32_INVALID_VOLUME)
    {
        return IO_NO_DEVICE_FOUND;
    }

    if (code == FAT32_NO_SPACE || code == FAT32_ALREADY_EXISTS || code == FAT32_FILE_TOO_LARGE)
    {
        return IO_OPERATION_NOT_SUPPORTED;
    }

    return IO_OPERATION_NOT_SUPPORTED;
}

static bool Fat32FilesystemIO__is_root_path(KString path)
{
    return path.len == 0 || (path.len == 1 && path.data[0] == '/');
}

static bool Fat32FilesystemIO__is_root_file_path(KString path)
{
    if (path.len == 0)
    {
        return false;
    }

    if (path.data[0] == '/' || path.data[path.len - 1] == '/')
    {
        return false;
    }

    return true;
}

typedef struct Fat32AsyncOpenContext
{
    Fat32FilesystemIOStatePtr fs_state;
    KString path;
    Flags flags;
}
Fat32AsyncOpenContext;

typedef Fat32AsyncOpenContext * Fat32AsyncOpenContextPtr;

typedef struct Fat32AsyncReadContext
{
    Fat32FilesystemIOStatePtr fs_state;
    Fat32FileHandlePtr file_handle;
    ExternalBuffer target_buffer;
    size_t requested_size;
    size_t read_size;
    uint32_t file_cluster;
    uint32_t target_cluster;
    uint32_t file_size;
    uint32_t cluster_hops;
}
Fat32AsyncReadContext;

typedef Fat32AsyncReadContext * Fat32AsyncReadContextPtr;

typedef struct Fat32AsyncWriteContext
{
    Fat32FilesystemIOStatePtr fs_state;
    Fat32FileHandlePtr file_handle;
    size_t size;
    Data encoded_name[11];
    Data file_sector[FAT32_BYTES_PER_SECTOR];
    Data fat_sector[FAT32_BYTES_PER_SECTOR];
    Data directory_sector[FAT32_BYTES_PER_SECTOR];
    uint32_t cluster;
    BlockNumber fat_relative_sector;
    size_t fat_sector_offset;
    uint8_t fat_copy_index;
    BlockNumber entry_sector;
    size_t entry_offset;
    KernelContext kernel_context;
    bool kernel_context_set;
}
Fat32AsyncWriteContext;

typedef Fat32AsyncWriteContext * Fat32AsyncWriteContextPtr;

static Fat32FileHandlePtr Fat32FileHandle__heap_new(KString path, Flags flags)
{
    Fat32FileHandlePtr handle = ptr(Kernel__alloc_kernel(sizeof(Fat32FileHandle)), Fat32FileHandle);
    *handle = (Fat32FileHandle) {
        .path = path,
        .offset = 0,
        .buffer = EmptyRingBuffer(IOBuffer),
        .flags = flags,
    };

    return handle;
}

static bool Fat32FilesystemIOState__uses_userspace_device(Fat32FilesystemIOStatePtr self);
static Result(IOInternalHandle) Fat32FilesystemIOState__add_handle(
    Fat32FilesystemIOStatePtr self, KString path, Flags flags
);
static Result(Fat32FileHandlePtr) Fat32FilesystemIOState__get_handle(
    Fat32FilesystemIOStatePtr self, IOInternalHandle handle
);
static Result(IOInternalHandle) Fat32FilesystemIOState__delete_handle(
    Fat32FilesystemIOStatePtr self, IOInternalHandle handle
);
static Result(Anything) Fat32FileIO__complete_async_open(BlockIOContextPtr block_context);
static Result(Anything) Fat32FileIO__complete_async_list_dir(BlockIOContextPtr block_context);
static Result(Anything) Fat32FileIO__complete_async_read_directory(BlockIOContextPtr block_context);
static Result(Anything) Fat32FileIO__complete_async_read_fat_for_file(BlockIOContextPtr block_context);
static Result(Anything) Fat32FileIO__complete_async_read_file(BlockIOContextPtr block_context);
static Result(Anything) Fat32FileIO__complete_async_write_directory_read(BlockIOContextPtr block_context);
static Result(Anything) Fat32FileIO__complete_async_write_directory_scanned(BlockIOContextPtr block_context);
static Result(Anything) Fat32FileIO__complete_async_write_fat_read(BlockIOContextPtr block_context);
static Result(Anything) Fat32FileIO__complete_async_write_fat_written(BlockIOContextPtr block_context);
static Result(Anything) Fat32FileIO__complete_async_write_file_written(BlockIOContextPtr block_context);
static Result(Anything) Fat32FileIO__complete_async_write_directory_written(BlockIOContextPtr block_context);

static Result(IOBufferPtr) Fat32FileIO__start_async_read_with_device(
    Fat32FilesystemIOStatePtr fs_state,
    Fat32FileHandlePtr file_handle,
    BlockDevicePtr device,
    size_t size,
    ExternalBuffer target_buffer
)
{
    Fat32AsyncReadContextPtr read_context =
        ptr(Kernel__alloc_kernel(sizeof(Fat32AsyncReadContext)), Fat32AsyncReadContext);
    *read_context = (Fat32AsyncReadContext) {
        .fs_state = fs_state,
        .file_handle = file_handle,
        .target_buffer = target_buffer,
        .requested_size = size,
        .read_size = 0,
        .file_cluster = 0,
        .target_cluster = 0,
        .file_size = 0,
        .cluster_hops = 0,
    };

    BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    block_context->kernel_context = EMPTY_KERNEL_CONTEXT;
    block_context->owner_context = (Anything) read_context;

    BlockNumber root_sector = Fat32Volume__cluster_sector(&fs_state->volume, fs_state->volume.root_cluster);
    __d_printf(
        FAT32,
        "read file path=%s offset=%i requested=%i root_sector=%i\n",
        cstr(file_handle->path),
        (int) file_handle->offset,
        (int) size,
        (int) root_sector
    );

    ResultMatch(Anything, BlockDevice__read_async(
        device,
        root_sector,
        1,
        block_context,
        Fat32FileIO__complete_async_read_directory
    ))
        IsSuccess(count, return Error(IOBufferPtr, io_error, IO_WAIT_FOR_DATA))
        IsError(code, message, return Error(IOBufferPtr, message, code))
    End

    return Error(IOBufferPtr, io_error, IO_PANIC);
}

static Result(IOBufferPtr) Fat32FileIO__start_async_read(
    Fat32FilesystemIOStatePtr fs_state,
    Fat32FileHandlePtr file_handle,
    size_t size,
    ExternalBuffer target_buffer
)
{
    Match(BlockDevicePtr, fs_state->volume.device)
        IsSome(device, return Fat32FileIO__start_async_read_with_device(
            fs_state,
            file_handle,
            device,
            size,
            target_buffer
        ))
        IsNone(return Error(IOBufferPtr, invalid_path, IO_INVALID_PATH))
    End

    return Error(IOBufferPtr, invalid_path, IO_INVALID_PATH);
}

static Result(Anything) Fat32FileIO__async_read_sector_for_file(
    Fat32AsyncReadContextPtr context,
    KernelContext kernel_context,
    BlockNumber sector,
    BlockIOContinuation continuation
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr file_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            file_context->kernel_context = kernel_context;
            file_context->owner_context = (Anything) context;

            BlockDevice__set_current_context(&file_context->kernel_context);
            Result(Anything) read_result = BlockDevice__read_async(device, sector, 1, file_context, continuation);
            BlockDevice__clear_current_context();
            return read_result;
        })
        IsNone(return Error(Anything, invalid_path, IO_INVALID_PATH))
    End

    return Error(Anything, invalid_path, IO_INVALID_PATH);
}

static Result(Anything) Fat32FileIO__async_read_fat_for_file(
    Fat32AsyncReadContextPtr context,
    KernelContext kernel_context
)
{
    size_t fat_offset = context->target_cluster * sizeof(uint32_t);
    BlockNumber fat_sector = context->fs_state->volume.fat_start_sector + (fat_offset / FAT32_BYTES_PER_SECTOR);
    return Fat32FileIO__async_read_sector_for_file(
        context,
        kernel_context,
        fat_sector,
        Fat32FileIO__complete_async_read_fat_for_file
    );
}

static Result(Anything) Fat32FileIO__async_read_target_file_sector(
    Fat32AsyncReadContextPtr context,
    KernelContext kernel_context
)
{
    BlockNumber file_sector = Fat32Volume__cluster_sector(&context->fs_state->volume, context->target_cluster);
    __d_printf(
        FAT32,
        "read file cluster=%i sector=%i size=%i offset=%i copy=%i\n",
        (int) context->target_cluster,
        (int) file_sector,
        (int) context->file_size,
        (int) context->file_handle->offset,
        (int) context->read_size
    );
    return Fat32FileIO__async_read_sector_for_file(
        context,
        kernel_context,
        file_sector,
        Fat32FileIO__complete_async_read_file
    );
}

static Result(Anything) Fat32FileIO__async_read_sector_with_device(
    Fat32AsyncWriteContextPtr context,
    BlockDevicePtr device,
    BlockNumber sector,
    BlockIOContinuation continuation
)
{
    BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    block_context->owner_context = (Anything) context;

    __d_printf(
        FAT32,
        "async write read sector context=%p sector=%i continuation=%p kernel_context_set=%i\n",
        context,
        (int) sector,
        continuation,
        (int) context->kernel_context_set
    );

    Result(Anything) read_result;
    if (context->kernel_context_set)
    {
        block_context->kernel_context = context->kernel_context;
        BlockDevice__set_current_context(&block_context->kernel_context);
        read_result = BlockDevice__read_async(device, sector, 1, block_context, continuation);
        BlockDevice__clear_current_context();
    }
    else
    {
        read_result = BlockDevice__read_async(device, sector, 1, block_context, continuation);
    }
    return read_result;
}

static Result(Anything) Fat32FileIO__async_read_sector(
    Fat32AsyncWriteContextPtr context,
    BlockNumber sector,
    BlockIOContinuation continuation
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device, return Fat32FileIO__async_read_sector_with_device(context, device, sector, continuation))
        IsNone(return Error(Anything, invalid_path, IO_INVALID_PATH))
    End

    return Error(Anything, invalid_path, IO_INVALID_PATH);
}

static Result(Anything) Fat32FileIO__async_write_sector_with_device(
    Fat32AsyncWriteContextPtr context,
    BlockDevicePtr device,
    BlockNumber sector,
    DataPtr source,
    BlockIOContinuation continuation
)
{
    BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    block_context->owner_context = (Anything) context;

    __d_printf(
        FAT32,
        "async write write sector context=%p sector=%i source=%p continuation=%p kernel_context_set=%i\n",
        context,
        (int) sector,
        source,
        continuation,
        (int) context->kernel_context_set
    );

    Result(Anything) write_result;
    if (context->kernel_context_set)
    {
        block_context->kernel_context = context->kernel_context;
        BlockDevice__set_current_context(&block_context->kernel_context);
        write_result = BlockDevice__write_async(device, sector, 1, source, block_context, continuation);
        BlockDevice__clear_current_context();
    }
    else
    {
        write_result = BlockDevice__write_async(device, sector, 1, source, block_context, continuation);
    }
    return write_result;
}

static Result(Anything) Fat32FileIO__async_write_sector(
    Fat32AsyncWriteContextPtr context,
    BlockNumber sector,
    DataPtr source,
    BlockIOContinuation continuation
)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device, return Fat32FileIO__async_write_sector_with_device(
            context,
            device,
            sector,
            source,
            continuation
        ))
        IsNone(return Error(Anything, invalid_path, IO_INVALID_PATH))
    End

    return Error(Anything, invalid_path, IO_INVALID_PATH);
}

static Result(Anything) Fat32FileIO__start_async_write(
    Fat32FilesystemIOStatePtr fs_state,
    Fat32FileHandlePtr file_handle,
    DataPtr data,
    size_t size
)
{
    if (!Fat32Volume__has_valid_geometry(&fs_state->volume))
    {
        __d_printf(FAT32, "async write invalid geometry path=%s size=%i\n", cstr(file_handle->path), (int) size);
        return Error(Anything, invalid_argument, IO_INVALID_PATH);
    }

    if (size > FAT32_MAX_FILE_SIZE)
    {
        __d_printf(FAT32, "async write file too large path=%s size=%i\n", cstr(file_handle->path), (int) size);
        return Error(Anything, file_too_large, IO_OPERATION_NOT_SUPPORTED);
    }

    Fat32AsyncWriteContextPtr context =
        ptr(Kernel__alloc_kernel(sizeof(Fat32AsyncWriteContext)), Fat32AsyncWriteContext);
    *context = (Fat32AsyncWriteContext) {
        .fs_state = fs_state,
        .file_handle = file_handle,
        .size = size,
        .cluster = 0,
        .fat_relative_sector = 0,
        .fat_sector_offset = 0,
        .fat_copy_index = 0,
        .entry_sector = Fat32Volume__cluster_sector(&fs_state->volume, fs_state->volume.root_cluster),
        .entry_offset = FAT32_BYTES_PER_SECTOR,
        .kernel_context = EMPTY_KERNEL_CONTEXT,
        .kernel_context_set = false,
    };

    ResultMatch(Fat32Code, Fat32__encode_83_name(&file_handle->path, context->encoded_name))
        IsError(code, message, return Error(Anything, message, Fat32FilesystemIO__status_from_fat32(code)))
    End

    __d_printf(
        FAT32,
        "async write start context=%p path=%s size=%i root_sector=%i fat_start=%i sectors_per_fat=%i clusters=%i\n",
        context,
        cstr(file_handle->path),
        (int) size,
        (int) context->entry_sector,
        (int) fs_state->volume.fat_start_sector,
        (int) fs_state->volume.sectors_per_fat,
        (int) fs_state->volume.cluster_count
    );

    Fat32__zero(context->file_sector, FAT32_BYTES_PER_SECTOR);
    if (size > 0)
    {
        Fat32__copy(context->file_sector, data, size);
    }

    return Fat32FileIO__async_read_sector(
        context,
        context->entry_sector,
        Fat32FileIO__complete_async_write_directory_scanned
    );
}

static Result(Anything) Fat32FileIO__complete_async_write_directory_scanned(BlockIOContextPtr block_context)
{
    Fat32AsyncWriteContextPtr context = ptr(block_context->owner_context, Fat32AsyncWriteContext);
    __d_printf(
        FAT32,
        "async write directory scanned context=%p block=%i status=%i completed=%i request_id_present=%i\n",
        context,
        (int) block_context->block,
        (int) block_context->status,
        (int) block_context->completed_blocks,
        (int) block_context->request_id.present
    );
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    context->kernel_context = block_context->kernel_context;
    context->kernel_context_set = true;
    context->entry_offset = FAT32_BYTES_PER_SECTOR;
    for (size_t offset = 0; offset < FAT32_BYTES_PER_SECTOR; offset += FAT32_ROOT_ENTRY_SIZE)
    {
        if (block_context->buffer[offset] == 0x00)
        {
            context->entry_offset = offset;
            break;
        }

        if (block_context->buffer[offset] == 0xe5)
        {
            if (context->entry_offset >= FAT32_BYTES_PER_SECTOR)
            {
                context->entry_offset = offset;
            }
            continue;
        }

        bool matches = true;
        for (size_t i = 0; i < sizeof(context->encoded_name); i++)
        {
            if (block_context->buffer[offset + i] != context->encoded_name[i])
            {
                matches = false;
                break;
            }
        }

        if (matches)
        {
            __d_printf(FAT32, "async write duplicate context=%p entry_offset=%i\n", context, (int) offset);
            return Error(Anything, already_exists, IO_OPERATION_NOT_SUPPORTED);
        }
    }

    if (context->entry_offset >= FAT32_BYTES_PER_SECTOR)
    {
        __d_printf(FAT32, "async write no free directory entry context=%p\n", context);
        return Error(Anything, no_space, IO_OPERATION_NOT_SUPPORTED);
    }

    __d_printf(FAT32, "async write directory slot context=%p sector=%i offset=%i\n", context, (int) context->entry_sector, (int) context->entry_offset);
    context->fat_relative_sector = 0;
    return Fat32FileIO__async_read_sector(
        context,
        context->fs_state->volume.fat_start_sector,
        Fat32FileIO__complete_async_write_fat_read
    );
}

static Result(Anything) Fat32FileIO__complete_async_write_fat_read(BlockIOContextPtr block_context)
{
    Fat32AsyncWriteContextPtr context = ptr(block_context->owner_context, Fat32AsyncWriteContext);
    __d_printf(
        FAT32,
        "async write fat read context=%p block=%i status=%i completed=%i relative=%i\n",
        context,
        (int) block_context->block,
        (int) block_context->status,
        (int) block_context->completed_blocks,
        (int) context->fat_relative_sector
    );
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    context->kernel_context = block_context->kernel_context;
    context->kernel_context_set = true;
    Fat32__copy(context->fat_sector, block_context->buffer, FAT32_BYTES_PER_SECTOR);

    size_t entries_per_sector = FAT32_BYTES_PER_SECTOR / sizeof(uint32_t);
    for (size_t entry = 0; entry < entries_per_sector; entry++)
    {
        uint32_t cluster = (uint32_t) (context->fat_relative_sector * entries_per_sector + entry);
        if (cluster < 2)
        {
            continue;
        }

        if (cluster >= context->fs_state->volume.cluster_count + 2)
        {
            __d_printf(FAT32, "async write no free cluster context=%p cluster=%i\n", context, (int) cluster);
            return Error(Anything, no_space, IO_OPERATION_NOT_SUPPORTED);
        }

        uint32_t value = Fat32__read_u32(block_context->buffer, entry * sizeof(uint32_t)) & 0x0fffffff;
        if (value == FAT32_EMPTY_CLUSTER)
        {
            context->cluster = cluster;
            context->fat_sector_offset = entry * sizeof(uint32_t);
            Fat32__write_u32(context->fat_sector, context->fat_sector_offset, FAT32_END_OF_CHAIN);
            context->fat_copy_index = 0;
            __d_printf(
                FAT32,
                "async write selected cluster context=%p cluster=%i fat_relative=%i fat_offset=%i\n",
                context,
                (int) context->cluster,
                (int) context->fat_relative_sector,
                (int) context->fat_sector_offset
            );
            return Fat32FileIO__async_write_sector(
                context,
                context->fs_state->volume.fat_start_sector + context->fat_relative_sector,
                context->fat_sector,
                Fat32FileIO__complete_async_write_fat_written
            );
        }
    }

    context->fat_relative_sector++;
    if (context->fat_relative_sector >= context->fs_state->volume.sectors_per_fat)
    {
        __d_printf(FAT32, "async write exhausted fat context=%p\n", context);
        return Error(Anything, no_space, IO_OPERATION_NOT_SUPPORTED);
    }

    __d_printf(FAT32, "async write next fat sector context=%p relative=%i\n", context, (int) context->fat_relative_sector);
    return Fat32FileIO__async_read_sector(
        context,
        context->fs_state->volume.fat_start_sector + context->fat_relative_sector,
        Fat32FileIO__complete_async_write_fat_read
    );
}

static Result(Anything) Fat32FileIO__complete_async_write_fat_written(BlockIOContextPtr block_context)
{
    Fat32AsyncWriteContextPtr context = ptr(block_context->owner_context, Fat32AsyncWriteContext);
    __d_printf(
        FAT32,
        "async write fat written context=%p block=%i status=%i completed=%i copy_index=%i\n",
        context,
        (int) block_context->block,
        (int) block_context->status,
        (int) block_context->completed_blocks,
        (int) context->fat_copy_index
    );
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    context->kernel_context = block_context->kernel_context;
    context->kernel_context_set = true;
    context->fat_copy_index++;
    if (context->fat_copy_index < FAT32_FAT_COUNT)
    {
        BlockNumber fat_sector = context->fs_state->volume.fat_start_sector
            + (context->fat_copy_index * context->fs_state->volume.sectors_per_fat)
            + context->fat_relative_sector;
        __d_printf(FAT32, "async write next fat copy context=%p copy=%i sector=%i\n", context, (int) context->fat_copy_index, (int) fat_sector);
        return Fat32FileIO__async_write_sector(
            context,
            fat_sector,
            context->fat_sector,
            Fat32FileIO__complete_async_write_fat_written
        );
    }

    __d_printf(FAT32, "async write writing file context=%p cluster=%i sector=%i\n", context, (int) context->cluster, (int) Fat32Volume__cluster_sector(&context->fs_state->volume, context->cluster));
    return Fat32FileIO__async_write_sector(
        context,
        Fat32Volume__cluster_sector(&context->fs_state->volume, context->cluster),
        context->file_sector,
        Fat32FileIO__complete_async_write_file_written
    );
}

static Result(Anything) Fat32FileIO__complete_async_write_file_written(BlockIOContextPtr block_context)
{
    Fat32AsyncWriteContextPtr context = ptr(block_context->owner_context, Fat32AsyncWriteContext);
    __d_printf(
        FAT32,
        "async write file written context=%p block=%i status=%i completed=%i\n",
        context,
        (int) block_context->block,
        (int) block_context->status,
        (int) block_context->completed_blocks
    );
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    context->kernel_context = block_context->kernel_context;
    context->kernel_context_set = true;
    return Fat32FileIO__async_read_sector(
        context,
        context->entry_sector,
        Fat32FileIO__complete_async_write_directory_read
    );
}

static Result(Anything) Fat32FileIO__complete_async_write_directory_read(BlockIOContextPtr block_context)
{
    Fat32AsyncWriteContextPtr context = ptr(block_context->owner_context, Fat32AsyncWriteContext);
    __d_printf(
        FAT32,
        "async write directory read context=%p block=%i status=%i completed=%i entry_offset=%i\n",
        context,
        (int) block_context->block,
        (int) block_context->status,
        (int) block_context->completed_blocks,
        (int) context->entry_offset
    );
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    context->kernel_context = block_context->kernel_context;
    context->kernel_context_set = true;
    Fat32__copy(context->directory_sector, block_context->buffer, FAT32_BYTES_PER_SECTOR);
    Fat32__copy(&context->directory_sector[context->entry_offset], context->encoded_name, sizeof(context->encoded_name));
    context->directory_sector[context->entry_offset + 11] = FAT32_ATTR_ARCHIVE;
    Fat32__write_directory_cluster(&context->directory_sector[context->entry_offset], context->cluster);
    Fat32__write_u32(context->directory_sector, context->entry_offset + 28, (uint32_t) context->size);

    __d_printf(FAT32, "async write writing directory context=%p sector=%i cluster=%i size=%i\n", context, (int) context->entry_sector, (int) context->cluster, (int) context->size);
    return Fat32FileIO__async_write_sector(
        context,
        context->entry_sector,
        context->directory_sector,
        Fat32FileIO__complete_async_write_directory_written
    );
}

static Result(Anything) Fat32FileIO__complete_async_write_directory_written(BlockIOContextPtr block_context)
{
    Fat32AsyncWriteContextPtr context = ptr(block_context->owner_context, Fat32AsyncWriteContext);
    __d_printf(
        FAT32,
        "async write directory written context=%p block=%i status=%i completed=%i size=%i\n",
        context,
        (int) block_context->block,
        (int) block_context->status,
        (int) block_context->completed_blocks,
        (int) context->size
    );
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    context->file_handle->offset = context->size;
    return Success(Anything, context->size);
}

static Result(IOInternalHandle) Fat32FileIO__open(IOStatePtr self, KString path, Flags flags)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;

    if (!Fat32FilesystemIO__is_root_file_path(path))
    {
        return Error(IOInternalHandle, invalid_path, IO_INVALID_PATH);
    }

    if (!fs_state->mounted && !Fat32FilesystemIOState__uses_userspace_device(fs_state))
    {
        return Error(IOInternalHandle, invalid_path, IO_INVALID_PATH);
    }

    if (Fat32FilesystemIOState__uses_userspace_device(fs_state))
    {
        if (fs_state->mounted)
        {
            return Fat32FilesystemIOState__add_handle(fs_state, path, flags);
        }

        Match(BlockDevicePtr, fs_state->volume.device)
            IsSome(device,
            {
                Fat32AsyncOpenContextPtr open_context =
                    ptr(Kernel__alloc_kernel(sizeof(Fat32AsyncOpenContext)), Fat32AsyncOpenContext);
                open_context->fs_state = fs_state;
                open_context->path = path;
                open_context->flags = flags;

                BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
                block_context->kernel_context = EMPTY_KERNEL_CONTEXT;
                block_context->owner_context = (Anything) open_context;

                ResultMatch(Anything, BlockDevice__read_async(
                    device,
                    0,
                    1,
                    block_context,
                    Fat32FileIO__complete_async_open
                ))
                    IsSuccess(handle, return Success(IOInternalHandle, handle))
                    IsError(code, message, return Error(IOInternalHandle, message, code))
                End
            })
            IsNone(return Error(IOInternalHandle, invalid_path, IO_INVALID_PATH))
        End
    }

    if ((flags & IO_OPEN_READ) != 0)
    {
        BlockNumber entry_sector = 0;
        size_t entry_offset = 0;
        ResultMatch(Fat32Code, Fat32Volume__find_entry_at_path(&fs_state->volume, path, &entry_sector, &entry_offset))
            IsError(code, message, return Error(
                IOInternalHandle,
                message,
                Fat32FilesystemIO__status_from_fat32(code)
            ))
        End

        uint32_t cluster = 0;
        uint8_t attributes = 0;
        ResultMatch(Fat32Code, Fat32Volume__entry_cluster_and_attributes(
            &fs_state->volume,
            entry_sector,
            entry_offset,
            &cluster,
            &attributes
        ))
            IsError(code, message, return Error(
                IOInternalHandle,
                message,
                Fat32FilesystemIO__status_from_fat32(code)
            ))
        End

        if ((attributes & FAT32_ATTR_DIRECTORY) != 0 || cluster < 2)
        {
            return Error(IOInternalHandle, invalid_path, IO_INVALID_PATH);
        }
    }
    else if ((flags & IO_OPEN_WRITE) != 0)
    {
        uint32_t parent_cluster = 0;
        KString leaf_name = EMPTY_KSTRING;
        ResultMatch(Fat32Code, Fat32Volume__resolve_parent(
            &fs_state->volume,
            path,
            &parent_cluster,
            &leaf_name
        ))
            IsError(code, message, return Error(
                IOInternalHandle,
                message,
                Fat32FilesystemIO__status_from_fat32(code)
            ))
        End
    }

    CriticalSection(fs_state->state.lock,
    {
        IOInternalHandle handle = (IOInternalHandle) fs_state->handle_count++ | fs_state->state.filesystem->fs_code;
        Fat32FileHandleMap__set(&fs_state->handles, handle, Fat32FileHandle__heap_new(path, flags));
        _cs_return Success(IOInternalHandle, handle);
    })
}

static Result(IOInternalHandle) Fat32FileIO__close(IOStatePtr self, IOInternalHandle handle)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;

    __d_printf(FAT32, "Fat32FileIO__close start handle=%i\n", handle);
    return Fat32FilesystemIOState__delete_handle(fs_state, handle);
}

static Result(IOBufferPtr) Fat32FileIO__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ExternalBuffer target_buffer
)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;

    if (Fat32FilesystemIOState__uses_userspace_device(fs_state))
    {
        ResultMatch(Fat32FileHandlePtr, Fat32FilesystemIOState__get_handle(fs_state, handle))
            IsSuccess(target,
            {
                if ((target->flags & IO_OPEN_READ) == 0)
                {
                    return Error(IOBufferPtr, invalid_handle, IO_OPERATION_NOT_SUPPORTED);
                }
                return Fat32FileIO__start_async_read(fs_state, target, size, target_buffer);
            })
            IsError(code, message, return Error(IOBufferPtr, message, code))
        End
    }

    CriticalSection(fs_state->state.lock,
    {
        Match(Fat32FileHandlePtr, Fat32FileHandleMap__get(&fs_state->handles, handle))
            IsNone(_cs_return Error(IOBufferPtr, invalid_handle, IO_UNKNOWN_HANDLE))
            IsSome(target,
            {
                if ((target->flags & IO_OPEN_READ) == 0)
                {
                    _cs_return Error(IOBufferPtr, invalid_handle, IO_OPERATION_NOT_SUPPORTED);
                }

                if (Fat32FilesystemIOState__uses_userspace_device(fs_state))
                {
                    _cs_return Fat32FileIO__start_async_read(fs_state, target, size, target_buffer);
                }

                DataPtr data = ptr(Kernel__alloc_kernel(FAT32_MAX_READ_SIZE), Data);
                uint32_t bytes_read = 0;
                ResultMatch(Fat32Code, Fat32Volume__read_root_file(
                    &fs_state->volume, target->path, data, FAT32_MAX_READ_SIZE, &bytes_read
                ))
                    IsError(code, message,
                    {
                        Kernel__free(addr(data));
                        _cs_return Error(IOBufferPtr, message, Fat32FilesystemIO__status_from_fat32(code));
                    })
                End

                if (target->offset >= bytes_read)
                {
                    Kernel__free(addr(data));
                    _cs_return Error(IOBufferPtr, not_found, IO_EOF);
                }

                size_t remaining = bytes_read - target->offset;
                size_t free_space = IOBuffer__free_space(&target->buffer);
                size_t read_size = Fat32__min_size(size, Fat32__min_size(remaining, free_space));
                IOBuffer__push_all(&target->buffer, &data[target->offset], read_size);
                target->offset += read_size;
                Kernel__free(addr(data));

                _cs_return Success(IOBufferPtr, &target->buffer);
            })
        End
    })

    return Error(IOBufferPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(ByteCount) Fat32FileIO__write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;

    if (Fat32FilesystemIOState__uses_userspace_device(fs_state))
    {
        ResultMatch(Fat32FileHandlePtr, Fat32FilesystemIOState__get_handle(fs_state, handle))
            IsSuccess(target,
            {
                if ((target->flags & IO_OPEN_WRITE) == 0 || target->offset != 0)
                {
                    return Error(ByteCount, invalid_handle, IO_OPERATION_NOT_SUPPORTED);
                }

                ResultMatch(Anything, Fat32FileIO__start_async_write(fs_state, target, data, size))
                    IsSuccess(count, return Error(ByteCount, io_error, IO_WAIT_FOR_DATA))
                    IsError(code, message, return Error(ByteCount, message, code))
                End
            })
            IsError(code, message, return Error(ByteCount, message, code))
        End
    }

    CriticalSection(fs_state->state.lock,
    {
        Match(Fat32FileHandlePtr, Fat32FileHandleMap__get(&fs_state->handles, handle))
            IsNone(_cs_return Error(ByteCount, invalid_handle, IO_UNKNOWN_HANDLE))
            IsSome(target,
            {
                if ((target->flags & IO_OPEN_WRITE) == 0 || target->offset != 0)
                {
                    _cs_return Error(ByteCount, invalid_handle, IO_OPERATION_NOT_SUPPORTED);
                }

                if (Fat32FilesystemIOState__uses_userspace_device(fs_state))
                {
                    ResultMatch(Anything, Fat32FileIO__start_async_write(fs_state, target, data, size))
                        IsSuccess(count, _cs_return Error(ByteCount, io_error, IO_WAIT_FOR_DATA))
                        IsError(code, message, _cs_return Error(ByteCount, message, code))
                    End
                }

                ResultMatch(Fat32Code, Fat32Volume__create_file_at_path(&fs_state->volume, target->path, data, size))
                    IsSuccess(_, {
                        target->offset = size;
                        _cs_return Success(ByteCount, size);
                    })
                    IsError(code, message, _cs_return Error(
                        ByteCount,
                        message,
                        Fat32FilesystemIO__status_from_fat32(code)
                    ))
                End
            })
        End
    })

    return Error(ByteCount, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(ParkStatus) Fat32FileIO__park(IOStatePtr self, IOContextPtr parked_context)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

static Result(ParkStatus) Fat32FileIO__unpark(IOStatePtr self)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

static Result(FilesystemNodes) Fat32FileIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, KString path)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;
    FilesystemNodes result = EmptyArray(FilesystemNodes);

    if (!fs_state->mounted)
    {
        return Error(FilesystemNodes, invalid_path, IO_INVALID_PATH);
    }

    if (!Fat32FilesystemIO__is_root_path(path))
    {
        if (!Fat32FilesystemIO__is_root_file_path(path))
        {
            return Error(FilesystemNodes, invalid_path, IO_INVALID_PATH);
        }

        BlockNumber entry_sector = 0;
        size_t entry_offset = 0;
        ResultMatch(Fat32Code, Fat32Volume__find_entry_at_path(&fs_state->volume, path, &entry_sector, &entry_offset))
            IsError(code, message, return Error(
                FilesystemNodes,
                message,
                Fat32FilesystemIO__status_from_fat32(code)
            ))
        End

        uint32_t cluster = 0;
        uint8_t attributes = 0;
        ResultMatch(Fat32Code, Fat32Volume__entry_cluster_and_attributes(
            &fs_state->volume,
            entry_sector,
            entry_offset,
            &cluster,
            &attributes
        ))
            IsError(code, message, return Error(
                FilesystemNodes,
                message,
                Fat32FilesystemIO__status_from_fat32(code)
            ))
        End

        if ((attributes & FAT32_ATTR_DIRECTORY) == 0)
        {
            return Error(FilesystemNodes, invalid_path, IO_INVALID_PATH);
        }

        ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_directory(&fs_state->volume, cluster))
            IsSuccess(entries,
            {
                FAIterate(Fat32DirectoryEntries, entries, index, entry)
                    FilesystemNodes__push(
                        &result,
                        FilesystemNode__heap_new(fs_state->state.filesystem, entry.name, entry.attributes)
                    );
                End
                return Success(FilesystemNodes, result);
            })
            IsError(code, message, return Error(
                FilesystemNodes,
                message,
                Fat32FilesystemIO__status_from_fat32(code)
            ))
        End
    }

    ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_root(&fs_state->volume))
        IsSuccess(entries,
        {
            FAIterate(Fat32DirectoryEntries, entries, index, entry)
                FilesystemNodes__push(
                    &result,
                    FilesystemNode__heap_new(fs_state->state.filesystem, entry.name, entry.attributes)
                );
            End
            return Success(FilesystemNodes, result);
        })
        IsError(code, message, return Error(
            FilesystemNodes,
            message,
            Fat32FilesystemIO__status_from_fat32(code)
        ))
    End

    return Success(FilesystemNodes, result);
}

typedef struct Fat32ListDirContext
{
    Fat32FilesystemIOStatePtr fs_state;
    KString path;
    DirectoryEntryPtr entries;
    size_t max_entries;
    bool mount_first;
}
Fat32ListDirContext;

typedef Fat32ListDirContext * Fat32ListDirContextPtr;

static bool Fat32FilesystemIOState__uses_userspace_device(Fat32FilesystemIOStatePtr self)
{
    Match(BlockDevicePtr, self->volume.device)
        IsSome(device, return device->userspace_service)
    End

    return false;
}

static Result(IOInternalHandle) Fat32FilesystemIOState__add_handle(
    Fat32FilesystemIOStatePtr self, KString path, Flags flags
)
{
    Fat32FileHandlePtr handle = Fat32FileHandle__heap_new(path, flags);

    CriticalSection(self->state.lock,
    {
        IOInternalHandle io_handle = (IOInternalHandle) self->handle_count++ | self->state.filesystem->fs_code;
        Fat32FileHandleMap__set(&self->handles, io_handle, handle);
        _cs_return Success(IOInternalHandle, io_handle);
    })
}

static Result(Fat32FileHandlePtr) Fat32FilesystemIOState__get_handle(
    Fat32FilesystemIOStatePtr self, IOInternalHandle handle
)
{
    CriticalSection(self->state.lock,
    {
        Match(Fat32FileHandlePtr, Fat32FileHandleMap__get(&self->handles, handle))
            IsSome(file_handle, _cs_return Success(Fat32FileHandlePtr, file_handle))
            IsNone(_cs_return Error(Fat32FileHandlePtr, invalid_handle, IO_UNKNOWN_HANDLE))
        End
    })

    return Error(Fat32FileHandlePtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOInternalHandle) Fat32FilesystemIOState__delete_handle(
    Fat32FilesystemIOStatePtr self, IOInternalHandle handle
)
{
    CriticalSection(self->state.lock,
    {
        __d_printf(FAT32, "Fat32FileIO__close locked handle=%i\n", handle);
        Match(Fat32FileHandlePtr, Fat32FileHandleMap__get(&self->handles, handle))
            IsSome(_,
            {
                __d_printf(FAT32, "Fat32FileIO__close deleting handle=%i\n", handle);
                Fat32FileHandleMap__delete(&self->handles, handle);
                __d_printf(FAT32, "Fat32FileIO__close success handle=%i\n", handle);
                _cs_return Success(IOInternalHandle, handle);
            })
            IsNone(
            {
                __d_printf(FAT32, "Fat32FileIO__close unknown handle=%i\n", handle);
                _cs_return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE);
            })
        End
    })

    return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(Anything) Fat32FileIO__complete_async_open(BlockIOContextPtr block_context)
{
    Fat32AsyncOpenContextPtr context = ptr(block_context->owner_context, Fat32AsyncOpenContext);
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    ResultMatch(Fat32Code, Fat32Volume__mount_from_sector(&context->fs_state->volume, block_context->buffer))
        IsError(code, message, return Error(Anything, message, Fat32FilesystemIO__status_from_fat32(code)))
    End
    context->fs_state->mounted = true;

    ResultMatch(IOInternalHandle, Fat32FilesystemIOState__add_handle(
        context->fs_state, context->path, context->flags
    ))
        IsSuccess(handle, return Success(Anything, handle))
        IsError(code, message, return Error(Anything, message, code))
    End

    return Error(Anything, invalid_path, IO_INVALID_PATH);
}

static Result(Anything) Fat32FileIO__complete_async_read_directory(BlockIOContextPtr block_context)
{
    Fat32AsyncReadContextPtr context = ptr(block_context->owner_context, Fat32AsyncReadContext);
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    size_t entry_offset = FAT32_BYTES_PER_SECTOR;
    KString lfn_name = EMPTY_KSTRING;
    uint8_t lfn_checksum = 0;
    bool lfn_valid = false;
    for (size_t offset = 0; offset < FAT32_BYTES_PER_SECTOR; offset += FAT32_ROOT_ENTRY_SIZE)
    {
        if (block_context->buffer[offset] == 0x00)
        {
            return Error(Anything, not_found, IO_INVALID_PATH);
        }

        if (block_context->buffer[offset] == 0xe5)
        {
            Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
            continue;
        }

        if (Fat32__is_lfn_entry(&block_context->buffer[offset]))
        {
            Fat32__accumulate_lfn(&block_context->buffer[offset], &lfn_name, &lfn_checksum, &lfn_valid);
            continue;
        }

        if (!Fat32__is_visible_directory_entry(&block_context->buffer[offset]))
        {
            Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
            continue;
        }

        KString entry_name = Fat32__entry_name(&block_context->buffer[offset], &lfn_name, lfn_checksum, lfn_valid);
        if (Fat32__names_equal(&entry_name, &context->file_handle->path))
        {
            entry_offset = offset;
            break;
        }

        Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
    }

    if (entry_offset >= FAT32_BYTES_PER_SECTOR)
    {
        return Error(Anything, not_found, IO_INVALID_PATH);
    }

    uint32_t file_cluster = Fat32__read_directory_cluster(&block_context->buffer[entry_offset]);
    uint32_t file_size = Fat32__read_u32(block_context->buffer, entry_offset + 28);
    uint8_t attributes = block_context->buffer[entry_offset + 11];
    if ((attributes & FAT32_ATTR_DIRECTORY) != 0
        || file_cluster < 2
        || file_size > FAT32_MAX_READ_SIZE
        || context->file_handle->offset > file_size)
    {
        return Error(Anything, invalid_path, IO_INVALID_PATH);
    }

    if (context->file_handle->offset == file_size)
    {
        return Error(Anything, not_found, IO_EOF);
    }

    size_t cluster_offset = context->file_handle->offset % FAT32_BYTES_PER_SECTOR;
    size_t remaining_in_cluster = FAT32_BYTES_PER_SECTOR - cluster_offset;
    size_t remaining = file_size - context->file_handle->offset;
    context->read_size = Fat32__min_size(
        context->requested_size,
        Fat32__min_size(context->target_buffer.len, Fat32__min_size(remaining, remaining_in_cluster))
    );
    if (context->read_size == 0)
    {
        return Success(Anything, 0);
    }

    context->file_cluster = file_cluster;
    context->target_cluster = file_cluster;
    context->file_size = file_size;
    context->cluster_hops = (uint32_t) (context->file_handle->offset / FAT32_BYTES_PER_SECTOR);
    if (context->cluster_hops == 0)
    {
        return Fat32FileIO__async_read_target_file_sector(context, block_context->kernel_context);
    }

    return Fat32FileIO__async_read_fat_for_file(context, block_context->kernel_context);
}

static Result(Anything) Fat32FileIO__complete_async_read_fat_for_file(BlockIOContextPtr block_context)
{
    Fat32AsyncReadContextPtr context = ptr(block_context->owner_context, Fat32AsyncReadContext);
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    size_t fat_offset = context->target_cluster * sizeof(uint32_t);
    size_t sector_offset = fat_offset % FAT32_BYTES_PER_SECTOR;
    uint32_t next_cluster = Fat32__read_u32(block_context->buffer, sector_offset) & 0x0fffffff;
    if (Fat32__is_end_of_chain(next_cluster)
        || next_cluster == FAT32_EMPTY_CLUSTER
        || next_cluster < 2
        || next_cluster >= context->fs_state->volume.cluster_count + 2)
    {
        return Error(Anything, invalid_volume, IO_INVALID_PATH);
    }

    context->target_cluster = next_cluster;
    context->cluster_hops--;
    if (context->cluster_hops == 0)
    {
        return Fat32FileIO__async_read_target_file_sector(context, block_context->kernel_context);
    }

    return Fat32FileIO__async_read_fat_for_file(context, block_context->kernel_context);
}

static Result(Anything) Fat32FileIO__complete_async_read_file(BlockIOContextPtr block_context)
{
    Fat32AsyncReadContextPtr context = ptr(block_context->owner_context, Fat32AsyncReadContext);
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    size_t offset = context->file_handle->offset % FAT32_BYTES_PER_SECTOR;
    if (offset + context->read_size > block_context->buffer_size)
    {
        return Error(Anything, invalid_path, IO_INVALID_PATH);
    }

    Fat32__copy(context->target_buffer.data, &block_context->buffer[offset], context->read_size);
    context->file_handle->offset += context->read_size;
    __d_printf(FAT32, "read file complete bytes=%i\n", (int) context->read_size);
    return Success(Anything, context->read_size);
}

static DirectoryEntry Fat32FileIO__directory_entry_from_fat_entry(Fat32DirectoryEntryPtr node)
{
    DirectoryEntry entry = (DirectoryEntry) { 0 };
    entry.attributes = node->attributes;

    size_t max_name = IO_DIRENT_NAME_MAX - 1;
    size_t copy_len = node->name.len < max_name ? node->name.len : max_name;
    for (size_t i = 0; i < copy_len; i++)
    {
        entry.name[i] = node->name.data[i];
    }
    entry.name[copy_len] = '\0';
    entry.name_len = copy_len;

    return entry;
}

static DirectoryEntry Fat32FileIO__directory_entry_from_node(FilesystemNodePtr node)
{
    DirectoryEntry entry = (DirectoryEntry) { 0 };
    entry.attributes = node->attribs;

    size_t max_name = IO_DIRENT_NAME_MAX - 1;
    size_t copy_len = node->name.len < max_name ? node->name.len : max_name;
    for (size_t i = 0; i < copy_len; i++)
    {
        entry.name[i] = node->name.data[i];
    }
    entry.name[copy_len] = '\0';
    entry.name_len = copy_len;

    return entry;
}

static Result(DirectoryEntryCount) Fat32FileIO__copy_nodes_to_dirents(
    FilesystemNodes source, DirectoryEntryPtr entries, size_t max_entries
)
{
    DirectoryEntryCount count = 0;
    FAIterate(FilesystemNodes, source, index, node)
        if ((size_t) count >= max_entries)
        {
            return Success(DirectoryEntryCount, count);
        }
        entries[count] = Fat32FileIO__directory_entry_from_node(node);
        count++;
    End

    return Success(DirectoryEntryCount, count);
}

static Result(DirectoryEntryCount) Fat32FileIO__copy_entries_to_dirents(
    Fat32DirectoryEntries source, DirectoryEntryPtr entries, size_t max_entries
)
{
    DirectoryEntryCount count = 0;
    FAIterate(Fat32DirectoryEntries, source, index, entry)
        if ((size_t) count >= max_entries)
        {
            return Success(DirectoryEntryCount, count);
        }
        entries[count] = Fat32FileIO__directory_entry_from_fat_entry(&entry);
        count++;
    End

    return Success(DirectoryEntryCount, count);
}

static Result(DirectoryEntryCount) Fat32FileIO__copy_directory_sector_to_dirents(
    DataPtr sector, DirectoryEntryPtr entries, size_t max_entries
)
{
    DirectoryEntryCount count = 0;
    KString lfn_name = EMPTY_KSTRING;
    uint8_t lfn_checksum = 0;
    bool lfn_valid = false;
    for (size_t offset = 0; offset < FAT32_BYTES_PER_SECTOR; offset += FAT32_ROOT_ENTRY_SIZE)
    {
        if (sector[offset] == 0x00)
        {
            return Success(DirectoryEntryCount, count);
        }

        if (sector[offset] == 0xe5)
        {
            Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
            continue;
        }

        if (Fat32__is_lfn_entry(&sector[offset]))
        {
            Fat32__accumulate_lfn(&sector[offset], &lfn_name, &lfn_checksum, &lfn_valid);
            continue;
        }

        if (!Fat32__is_visible_directory_entry(&sector[offset]))
        {
            Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
            continue;
        }

        if ((size_t) count >= max_entries)
        {
            return Success(DirectoryEntryCount, count);
        }

        Fat32DirectoryEntry fat_entry = {
            .name = Fat32__entry_name(&sector[offset], &lfn_name, lfn_checksum, lfn_valid),
            .attributes = sector[offset + 11],
            .first_cluster = Fat32__read_directory_cluster(&sector[offset]),
            .size = Fat32__read_u32(sector, offset + 28),
        };
        entries[count] = Fat32FileIO__directory_entry_from_fat_entry(&fat_entry);
        count++;
        Fat32__clear_lfn(&lfn_name, &lfn_checksum, &lfn_valid);
    }

    return Success(DirectoryEntryCount, count);
}

static Result(Anything) Fat32FileIO__read_root_dir_async(Fat32ListDirContextPtr context, KernelContext kernel_context)
{
    Match(BlockDevicePtr, context->fs_state->volume.device)
        IsSome(device,
        {
            BlockIOContextPtr root_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            root_context->kernel_context = kernel_context;
            root_context->owner_context = (Anything) context;
            context->mount_first = false;

            BlockNumber root_sector = Fat32Volume__cluster_sector(
                &context->fs_state->volume,
                context->fs_state->volume.root_cluster
            );
            __d_printf(
                FAT32,
                "read root dir path=%s root_cluster=%i root_sector=%i data_start=%i sectors_per_fat=%i total=%i\n",
                cstr(context->path),
                (int) context->fs_state->volume.root_cluster,
                (int) root_sector,
                (int) context->fs_state->volume.data_start_sector,
                (int) context->fs_state->volume.sectors_per_fat,
                (int) context->fs_state->volume.total_sectors
            );

            BlockDevice__set_current_context(&root_context->kernel_context);
            Result(Anything) read_result = BlockDevice__read_async(
                device,
                root_sector,
                1,
                root_context,
                Fat32FileIO__complete_async_list_dir
            );
            BlockDevice__clear_current_context();
            return read_result;
        })
        IsNone(return Error(Anything, invalid_path, IO_INVALID_PATH))
    End

    return Error(Anything, invalid_path, IO_INVALID_PATH);
}

static Result(Anything) Fat32FileIO__complete_async_list_dir(BlockIOContextPtr block_context)
{
    Fat32ListDirContextPtr context = ptr(block_context->owner_context, Fat32ListDirContext);
    __d_printf(
        FAT32,
        "list complete block=%i blocks=%i status=%i mount_first=%i first=%x attr=%x\n",
        (int) block_context->block,
        (int) block_context->completed_blocks,
        (int) block_context->status,
        (int) context->mount_first,
        (int) block_context->buffer[0],
        (int) block_context->buffer[11]
    );
    if (block_context->status != BLOCK_DEVICE_OK || block_context->completed_blocks == 0)
    {
        return Error(Anything, io_error, IO_INVALID_PATH);
    }

    if (!Fat32FilesystemIO__is_root_path(context->path))
    {
        return Error(Anything, invalid_path, IO_INVALID_PATH);
    }

    if (context->mount_first)
    {
        ResultMatch(Fat32Code, Fat32Volume__mount_from_sector(&context->fs_state->volume, block_context->buffer))
            IsError(code, message, return Error(Anything, message, Fat32FilesystemIO__status_from_fat32(code)))
        End
        context->fs_state->mounted = true;
        __d_printf(
            FAT32,
            "mounted total=%i fat_start=%i sectors_per_fat=%i data_start=%i root_cluster=%i cluster_count=%i\n",
            (int) context->fs_state->volume.total_sectors,
            (int) context->fs_state->volume.fat_start_sector,
            (int) context->fs_state->volume.sectors_per_fat,
            (int) context->fs_state->volume.data_start_sector,
            (int) context->fs_state->volume.root_cluster,
            (int) context->fs_state->volume.cluster_count
        );
        return Fat32FileIO__read_root_dir_async(context, block_context->kernel_context);
    }

    ResultMatch(DirectoryEntryCount, Fat32FileIO__copy_directory_sector_to_dirents(
        block_context->buffer, context->entries, context->max_entries
    ))
        IsSuccess(count,
        {
            __d_printf(FAT32, "list decoded count=%i\n", (int) count);
            return Success(Anything, count);
        })
        IsError(code, message, return Error(Anything, message, code))
    End

    return Error(Anything, io_error, IO_INVALID_PATH);
}

static Result(DirectoryEntryCount) Fat32FileIO__list_dir(
    IOStatePtr self, KString path, DirectoryEntryPtr entries, size_t max_entries
)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;

    if (fs_state->mounted && !Fat32FilesystemIOState__uses_userspace_device(fs_state))
    {
        ResultMatch(FilesystemNodes, Fat32FileIO__list_nodes(self, fs_state->state.filesystem->root, path))
            IsSuccess(nodes, return Fat32FileIO__copy_nodes_to_dirents(nodes, entries, max_entries))
            IsError(code, message, return Error(
                DirectoryEntryCount,
                message,
                code
            ))
        End
    }

    if (!Fat32FilesystemIO__is_root_path(path))
    {
        return Error(DirectoryEntryCount, invalid_path, IO_INVALID_PATH);
    }

    Match(BlockDevicePtr, fs_state->volume.device)
        IsSome(device,
        {
            Fat32ListDirContextPtr list_context =
                ptr(Kernel__alloc_kernel(sizeof(Fat32ListDirContext)), Fat32ListDirContext);
            list_context->fs_state = fs_state;
            list_context->path = path;
            list_context->entries = entries;
            list_context->max_entries = max_entries;
            list_context->mount_first = !fs_state->mounted;

            BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
            block_context->kernel_context = EMPTY_KERNEL_CONTEXT;
            block_context->owner_context = (Anything) list_context;
            BlockNumber first_block =
                fs_state->mounted ? Fat32Volume__cluster_sector(&fs_state->volume, fs_state->volume.root_cluster) : 0;
            __d_printf(
                FAT32,
                "list path=%s mounted=%i userspace=%i mount_first=%i block=%i max=%i\n",
                cstr(path),
                (int) fs_state->mounted,
                (int) device->userspace_service,
                (int) list_context->mount_first,
                (int) first_block,
                (int) max_entries
            );

            ResultMatch(Anything, BlockDevice__read_async(
                device,
                first_block,
                1,
                block_context,
                Fat32FileIO__complete_async_list_dir
            ))
                IsSuccess(count, return Success(DirectoryEntryCount, count))
                IsError(code, message, return Error(DirectoryEntryCount, message, code))
            End
        })
        IsNone(return Error(DirectoryEntryCount, invalid_path, IO_INVALID_PATH))
    End

    return Error(DirectoryEntryCount, invalid_path, IO_INVALID_PATH);
}

static Result(IOStatus) Fat32FileIO__mkdir(IOStatePtr self, KString path)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;
    if (!fs_state->mounted || !Fat32FilesystemIO__is_root_file_path(path))
    {
        return Error(IOStatus, invalid_path, IO_INVALID_PATH);
    }

    ResultMatch(Fat32Code, Fat32Volume__create_directory_at_path(&fs_state->volume, path))
        IsSuccess(_, return Success(IOStatus, IO_OK))
        IsError(code, message, return Error(IOStatus, message, Fat32FilesystemIO__status_from_fat32(code)))
    End

    return Error(IOStatus, invalid_path, IO_INVALID_PATH);
}

static Result(IOStatus) Fat32FileIO__remove(IOStatePtr self, KString path, Flags flags)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;
    if (!fs_state->mounted || !Fat32FilesystemIO__is_root_file_path(path))
    {
        return Error(IOStatus, invalid_path, IO_INVALID_PATH);
    }

    CriticalSection(fs_state->state.lock,
    {
        ResultMatch(Fat32Code, Fat32Volume__remove_at_path(&fs_state->volume, path, flags))
            IsSuccess(_, _cs_return Success(IOStatus, IO_OK))
            IsError(code, message, _cs_return Error(IOStatus, message, Fat32FilesystemIO__status_from_fat32(code)))
        End
    })

    return Error(IOStatus, invalid_path, IO_INVALID_PATH);
}

FileIO Fat32FilesystemIO__new()
{
    return (FileIO) {
        .base_io = (IO) {
            .read = Fat32FileIO__read,
            .write = Fat32FileIO__write,
            .open = Fat32FileIO__open,
            .close = Fat32FileIO__close,
            .park = Fat32FileIO__park,
            .unpark = Fat32FileIO__unpark,
        },
        .list_nodes = Fat32FileIO__list_nodes,
        .list_dir = Fat32FileIO__list_dir,
        .mkdir = Fat32FileIO__mkdir,
        .remove = Fat32FileIO__remove,
    };
}

bool Fat32FilesystemIO__available()
{
    return true;
}

Fat32FilesystemIOStatePtr Fat32FilesystemIOState__heap_new(
    FilesystemPtr fs, BlockDevicePtr device, BlockCount total_sectors
)
{
    Fat32FilesystemIOStatePtr io_state = ptr(
        Kernel__alloc_kernel(sizeof(Fat32FilesystemIOState)), Fat32FilesystemIOState
    );

    if (total_sectors == 0)
    {
        total_sectors = FAT32_DEFAULT_MEMORY_SECTORS;
    }

    *io_state = (Fat32FilesystemIOState) {
        .state = FilesystemIOState__new(fs),
        .handles = Fat32FileHandleMap__new(),
        .handle_count = 0,
        .mounted = false,
    };

    io_state->volume = Fat32Volume__new(Some(BlockDevicePtr, device), total_sectors);

    if (!device->userspace_service)
    {
        ResultMatch(Fat32Code, Fat32Volume__mount(&io_state->volume))
            IsSuccess(_, io_state->mounted = true)
            IsError(mount_code, mount_message,
            {
                ResultMatch(Fat32Code, Fat32Volume__format(&io_state->volume))
                    IsSuccess(_,
                    {
                        ResultMatch(Fat32Code, Fat32Volume__mount(&io_state->volume))
                            IsSuccess(_, io_state->mounted = true)
                        End
                    })
                End
            })
        End
    }

    fs->state = &io_state->state;

    return io_state;
}
