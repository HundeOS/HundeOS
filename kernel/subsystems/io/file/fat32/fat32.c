#include "fat32.h"
#include "../../../../kalloc.h"

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

Fat32Volume Fat32Volume__new(BlockDevicePtr device, BlockCount total_sectors)
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
    return self != 0
        && self->device != 0
        && self->total_sectors > self->data_start_sector
        && self->sectors_per_fat > 0
        && self->cluster_count > 0;
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
    ResultMatch(BlockCount, BlockDevice__read(self->device, sector, 1, target))
        IsSuccess(count, {
            if (count == 1)
            {
                return Success(Fat32Code, FAT32_OK);
            }
        })
        IsError(code, message, return Error(Fat32Code, io_error, FAT32_IO_ERROR))
    End

    return Error(Fat32Code, io_error, FAT32_IO_ERROR);
}

static Result(Fat32Code) Fat32Volume__write_sector(Fat32VolumePtr self, BlockNumber sector, DataPtr source)
{
    ResultMatch(BlockCount, BlockDevice__write(self->device, sector, 1, source))
        IsSuccess(count, {
            if (count == 1)
            {
                return Success(Fat32Code, FAT32_OK);
            }
        })
        IsError(code, message, return Error(Fat32Code, io_error, FAT32_IO_ERROR))
    End

    return Error(Fat32Code, io_error, FAT32_IO_ERROR);
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
    if (name == 0 || name->len == 0)
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
    Data encoded_name[11] = { 0 };
    ResultMatch(Fat32Code, Fat32__encode_83_name(&name, encoded_name))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

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
            if (directory_sector[entry_offset] == 0x00)
            {
                return Error(Fat32Code, not_found, FAT32_NOT_FOUND);
            }

            if (directory_sector[entry_offset] == 0xe5)
            {
                continue;
            }

            bool matches = true;
            for (size_t i = 0; i < 11; i++)
            {
                if (directory_sector[entry_offset + i] != encoded_name[i])
                {
                    matches = false;
                    break;
                }
            }

            if (matches)
            {
                *sector = directory_sector_start + relative_sector;
                *offset = entry_offset;
                return Success(Fat32Code, FAT32_OK);
            }
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

Result(Fat32Code) Fat32Volume__mount(Fat32VolumePtr self)
{
    if (self == 0 || self->device == 0)
    {
        return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    Data sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    ResultMatch(Fat32Code, Fat32Volume__read_sector(self, 0, sector))
        IsError(code, message, return Error(Fat32Code, message, code))
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

            if (sector[offset] == 0xe5 || sector[offset + 11] == 0x0f)
            {
                continue;
            }

            Fat32DirectoryEntry entry = {
                .name = Fat32__decode_83_name(&sector[offset]),
                .attributes = sector[offset + 11],
                .first_cluster = Fat32__read_directory_cluster(&sector[offset]),
                .size = Fat32__read_u32(sector, offset + 28),
            };

            ResultMatch(CollectionCode, Fat32DirectoryEntries__push(&entries, entry))
                IsError(code, message, return Error(Fat32DirectoryEntries, message, code))
            End
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
    if (!Fat32Volume__has_valid_geometry(self) || parent_cluster == 0 || leaf_name == 0)
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
    *leaf_name = UnwrapPanic(KString, KStringArray__at(&segments, segment_count - 1));

    ResultMatch(Fat32Code, Fat32__encode_83_name(leaf_name, ((Data[11]) { 0 })))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    for (size_t i = 0; i < segment_count - 1; i++)
    {
        KString segment = UnwrapPanic(KString, KStringArray__at(&segments, i));
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
    if (!Fat32Volume__has_valid_geometry(self) || (data == 0 && size > 0))
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
    if (!Fat32Volume__has_valid_geometry(self) || target == 0 || bytes_read == 0)
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
    if (size > target_size || size > FAT32_MAX_FILE_SIZE || cluster < 2)
    {
        return Error(Fat32Code, invalid_argument, FAT32_INVALID_ARGUMENT);
    }

    Data file_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    ResultMatch(Fat32Code, Fat32Volume__read_sector(self, Fat32Volume__cluster_sector(self, cluster), file_sector))
        IsError(code, message, return Error(Fat32Code, message, code))
    End

    Fat32__copy(target, file_sector, size);
    *bytes_read = size;
    return Success(Fat32Code, FAT32_OK);
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

static Result(IOInternalHandle) Fat32FileIO__open(IOStatePtr self, KString path, Flags flags)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;

    if (fs_state == 0 || !fs_state->mounted || !Fat32FilesystemIO__is_root_file_path(path))
    {
        return Error(IOInternalHandle, invalid_path, IO_INVALID_PATH);
    }

    if ((flags & IO_OPEN_READ) != 0)
    {
        Data data[FAT32_MAX_FILE_SIZE] = { 0 };
        uint32_t bytes_read = 0;
        ResultMatch(Fat32Code, Fat32Volume__read_root_file(&fs_state->volume, path, data, sizeof(data), &bytes_read))
            IsError(code, message, return Error(
                IOInternalHandle,
                message,
                Fat32FilesystemIO__status_from_fat32(code)
            ))
        End
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

    CriticalSection(fs_state->state.lock,
    {
        Match(Fat32FileHandlePtr, Fat32FileHandleMap__get(&fs_state->handles, handle))
            IsNone(_cs_return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE))
            IsSome(_,
            {
                Fat32FileHandleMap__delete(&fs_state->handles, handle);
                _cs_return Success(IOInternalHandle, handle);
            })
        End
    })

    return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOBufferPtr) Fat32FileIO__read(IOStatePtr self, IOInternalHandle handle, size_t size)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;

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

                Data data[FAT32_MAX_FILE_SIZE] = { 0 };
                uint32_t bytes_read = 0;
                ResultMatch(Fat32Code, Fat32Volume__read_root_file(
                    &fs_state->volume, target->path, data, sizeof(data), &bytes_read
                ))
                    IsError(code, message, _cs_return Error(
                        IOBufferPtr,
                        message,
                        Fat32FilesystemIO__status_from_fat32(code)
                    ))
                End

                if (target->offset >= bytes_read)
                {
                    _cs_return Error(IOBufferPtr, not_found, IO_EOF);
                }

                size_t remaining = bytes_read - target->offset;
                size_t free_space = IOBuffer__free_space(&target->buffer);
                size_t read_size = Fat32__min_size(size, Fat32__min_size(remaining, free_space));
                IOBuffer__push_all(&target->buffer, &data[target->offset], read_size);
                target->offset += read_size;

                _cs_return Success(IOBufferPtr, &target->buffer);
            })
        End
    })

    return Error(IOBufferPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(ByteCount) Fat32FileIO__write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;

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
    (void) self;
    (void) parked_context;
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

static Result(ParkStatus) Fat32FileIO__unpark(IOStatePtr self)
{
    (void) self;
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

static Result(FilesystemNodes) Fat32FileIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, KString path)
{
    (void) parent;
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;
    FilesystemNodes result = EmptyArray(FilesystemNodes);

    if (fs_state == 0 || !fs_state->mounted)
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

static Result(IOStatus) Fat32FileIO__mkdir(IOStatePtr self, KString path)
{
    Fat32FilesystemIOStatePtr fs_state = (Fat32FilesystemIOStatePtr) self;
    if (fs_state == 0 || !fs_state->mounted || !Fat32FilesystemIO__is_root_file_path(path))
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
    if (fs_state == 0 || !fs_state->mounted || !Fat32FilesystemIO__is_root_file_path(path))
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
        .mkdir = Fat32FileIO__mkdir,
        .remove = Fat32FileIO__remove,
    };
}

bool Fat32FilesystemIO__available()
{
    return true;
}

Fat32FilesystemIOStatePtr Fat32FilesystemIOState__heap_new(FilesystemPtr fs, BlockCount total_sectors)
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
        .memory_device = MemoryBlockDevice__new(),
        .handles = Fat32FileHandleMap__new(),
        .handle_count = 0,
        .mounted = false,
    };

    io_state->block_device = MemoryBlockDevice__as_block_device(&io_state->memory_device);
    io_state->volume = Fat32Volume__new(&io_state->block_device, total_sectors);

    ResultMatch(Fat32Code, Fat32Volume__format(&io_state->volume))
        IsSuccess(_,
        {
            ResultMatch(Fat32Code, Fat32Volume__mount(&io_state->volume))
                IsSuccess(_, io_state->mounted = true)
            End
        })
    End

    fs->state = &io_state->state;

    return io_state;
}
