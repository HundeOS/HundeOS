#include "fat16.h"
#include "../fat/fat_name.h"
#include "../../../../kalloc.h"
#include "../../../../panic.h"

FixedArrayBodyDefine(Fat16DirectoryEntries);
MapBodyDefine(Fat16FileHandleMap);

static BString invalid_argument = bstr("Invalid FAT16 argument");
static BString io_error = bstr("FAT16 block IO failed");
static BString invalid_volume = bstr("Invalid FAT16 volume");
static BString no_space = bstr("FAT16 volume has no free space");
static BString not_found = bstr("FAT16 file not found");
static BString already_exists = bstr("FAT16 file already exists");
static BString unsupported_name = bstr("FAT16 only supports 8.3 root names");
static BString file_too_large = bstr("FAT16 file is too large for this operation");
static BString invalid_path = bstr("Invalid FAT16 path");
static BString invalid_handle = bstr("Invalid FAT16 handle");
static BString not_implemented = bstr("FAT16 operation not implemented");

#define FAT16_DEFAULT_MEMORY_SECTORS 256

bool Fat16DirectoryEntry__equals(Fat16DirectoryEntryPtr self, Fat16DirectoryEntryPtr other)
{
    return BString__equals(&self->name, &other->name)
        && self->attributes == other->attributes
        && self->first_cluster == other->first_cluster
        && self->size == other->size;
}

Fat16DirectoryEntry Fat16DirectoryEntry__new(
    ConstBStringPtr name,
    uint8_t attributes,
    uint16_t first_cluster,
    uint32_t size
)
{
    return (Fat16DirectoryEntry) {
        .name = *name,
        .attributes = attributes,
        .first_cluster = first_cluster,
        .size = size,
    };
}

Fat16Volume Fat16Volume__new(BlockDevicePtr device, BlockCount total_sectors)
{
    uint16_t root_dir_sectors =
        (FAT16_ROOT_ENTRY_COUNT * FAT16_ROOT_ENTRY_SIZE + FAT16_BYTES_PER_SECTOR - 1) / FAT16_BYTES_PER_SECTOR;

    Fat16Volume volume = {
        .device = device,
        .total_sectors = total_sectors,
        .sectors_per_fat = 0,
        .root_dir_sectors = root_dir_sectors,
        .fat_start_sector = FAT16_RESERVED_SECTORS,
        .root_dir_start_sector = 0,
        .data_start_sector = 0,
        .cluster_count = 0,
    };

    if (total_sectors > FAT16_RESERVED_SECTORS + root_dir_sectors)
    {
        BlockCount data_sectors = total_sectors - FAT16_RESERVED_SECTORS - root_dir_sectors;
        uint16_t sectors_per_fat = 1;
        while (true)
        {
            BlockCount available_data_sectors =
                total_sectors - FAT16_RESERVED_SECTORS - (FAT16_FAT_COUNT * sectors_per_fat) - root_dir_sectors;
            BlockCount clusters = available_data_sectors / FAT16_SECTORS_PER_CLUSTER;
            BlockCount required_fat_bytes = (clusters + 2) * sizeof(uint16_t);
            BlockCount required_fat_sectors =
                (required_fat_bytes + FAT16_BYTES_PER_SECTOR - 1) / FAT16_BYTES_PER_SECTOR;

            if (required_fat_sectors <= sectors_per_fat || data_sectors == 0)
            {
                volume.sectors_per_fat = sectors_per_fat;
                volume.root_dir_start_sector = FAT16_RESERVED_SECTORS + (FAT16_FAT_COUNT * sectors_per_fat);
                volume.data_start_sector = volume.root_dir_start_sector + root_dir_sectors;
                volume.cluster_count = (uint16_t) clusters;
                break;
            }

            sectors_per_fat = (uint16_t) required_fat_sectors;
        }
    }

    return volume;
}

static bool Fat16Volume__has_valid_geometry(Fat16VolumePtr self)
{
    return self->total_sectors > self->data_start_sector
        && self->sectors_per_fat > 0
        && self->cluster_count > 0;
}

static void Fat16__zero(DataPtr data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        data[i] = 0;
    }
}

static void Fat16__copy(DataPtr target, DataPtr source, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        target[i] = source[i];
    }
}

static size_t Fat16__min_size(size_t left, size_t right)
{
    if (left < right)
    {
        return left;
    }

    return right;
}

static uint16_t Fat16__read_u16(DataPtr data, size_t offset)
{
    return (uint16_t) data[offset] | ((uint16_t) data[offset + 1] << 8);
}

static uint32_t Fat16__read_u32(DataPtr data, size_t offset)
{
    return (uint32_t) data[offset]
        | ((uint32_t) data[offset + 1] << 8)
        | ((uint32_t) data[offset + 2] << 16)
        | ((uint32_t) data[offset + 3] << 24);
}

static void Fat16__write_u16(DataPtr data, size_t offset, uint16_t value)
{
    data[offset] = (Data) (value & 0xff);
    data[offset + 1] = (Data) ((value >> 8) & 0xff);
}

static void Fat16__write_u32(DataPtr data, size_t offset, uint32_t value)
{
    data[offset] = (Data) (value & 0xff);
    data[offset + 1] = (Data) ((value >> 8) & 0xff);
    data[offset + 2] = (Data) ((value >> 16) & 0xff);
    data[offset + 3] = (Data) ((value >> 24) & 0xff);
}

static Result(Fat16Code) Fat16Volume__read_sector(Fat16VolumePtr self, BlockNumber sector, DataPtr target)
{
    match_result(BlockCount, BlockDevice__read(self->device, sector, 1, target),
        success(count)
        {
            if (count == 1)
            {
                return Success(Fat16Code, FAT16_OK);
            }
        },
        error(code, message) { return Error(Fat16Code, io_error, FAT16_IO_ERROR); }
    );

    return Error(Fat16Code, io_error, FAT16_IO_ERROR);
}

static Result(Fat16Code) Fat16Volume__write_sector(Fat16VolumePtr self, BlockNumber sector, DataPtr source)
{
    match_result(BlockCount, BlockDevice__write(self->device, sector, 1, source),
        success(count)
        {
            if (count == 1)
            {
                return Success(Fat16Code, FAT16_OK);
            }
        },
        error(code, message) { return Error(Fat16Code, io_error, FAT16_IO_ERROR); }
    );

    return Error(Fat16Code, io_error, FAT16_IO_ERROR);
}

static Result(Fat16Code) Fat16__encode_83_name(BStringPtr name, Data target[11])
{
    if (!FatName__encode_83(name, target))
    {
        return Error(Fat16Code, unsupported_name, FAT16_UNSUPPORTED_NAME);
    }

    return Success(Fat16Code, FAT16_OK);
}

static BString Fat16__decode_83_name(DataPtr source)
{
    BString result = empty_bstr();

    for (size_t i = 0; i < 8 && source[i] != ' '; i++)
    {
        BString__push_char(&result, (char) source[i]);
    }

    if (source[8] != ' ')
    {
        BString__push_char(&result, '.');

        for (size_t i = 8; i < 11 && source[i] != ' '; i++)
        {
            BString__push_char(&result, (char) source[i]);
        }
    }

    return result;
}

static Result(Fat16Code) Fat16Volume__write_fat_entry(Fat16VolumePtr self, uint16_t cluster, uint16_t value)
{
    Data sector[FAT16_BYTES_PER_SECTOR] = { 0 };
    size_t fat_offset = cluster * sizeof(uint16_t);
    BlockNumber relative_sector = fat_offset / FAT16_BYTES_PER_SECTOR;
    size_t sector_offset = fat_offset % FAT16_BYTES_PER_SECTOR;

    for (uint8_t fat = 0; fat < FAT16_FAT_COUNT; fat++)
    {
        BlockNumber fat_sector = self->fat_start_sector + (fat * self->sectors_per_fat) + relative_sector;

        match_result(Fat16Code, Fat16Volume__read_sector(self, fat_sector, sector),
            success_do_nothing(),
            error(code, message) { return Error(Fat16Code, message, code); }
        );

        Fat16__write_u16(sector, sector_offset, value);

        match_result(Fat16Code, Fat16Volume__write_sector(self, fat_sector, sector),
            success_do_nothing(),
            error(code, message) { return Error(Fat16Code, message, code); }
        );
    }

    return Success(Fat16Code, FAT16_OK);
}

static Result(Fat16Code) Fat16Volume__read_fat_entry(Fat16VolumePtr self, uint16_t cluster, uint16_t * value)
{
    Data sector[FAT16_BYTES_PER_SECTOR] = { 0 };
    size_t fat_offset = cluster * sizeof(uint16_t);
    BlockNumber relative_sector = fat_offset / FAT16_BYTES_PER_SECTOR;
    size_t sector_offset = fat_offset % FAT16_BYTES_PER_SECTOR;

    match_result(Fat16Code, Fat16Volume__read_sector(self, self->fat_start_sector + relative_sector, sector),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    *value = Fat16__read_u16(sector, sector_offset);
    return Success(Fat16Code, FAT16_OK);
}

static Result(Fat16Code) Fat16Volume__find_free_cluster(Fat16VolumePtr self, uint16_t * cluster)
{
    for (uint16_t current = 2; current < self->cluster_count + 2; current++)
    {
        uint16_t value = FAT16_END_OF_CHAIN;
        match_result(Fat16Code, Fat16Volume__read_fat_entry(self, current, &value),
            success_do_nothing(),
            error(code, message) { return Error(Fat16Code, message, code); }
        );

        if (value == FAT16_EMPTY_CLUSTER)
        {
            *cluster = current;
            return Success(Fat16Code, FAT16_OK);
        }
    }

    return Error(Fat16Code, no_space, FAT16_NO_SPACE);
}

static BlockNumber Fat16Volume__cluster_sector(Fat16VolumePtr self, uint16_t cluster)
{
    return self->data_start_sector + ((cluster - 2) * FAT16_SECTORS_PER_CLUSTER);
}

static Result(Fat16Code) Fat16Volume__find_root_entry(
    Fat16VolumePtr self, ConstBStringPtr name, BlockNumber * sector, size_t * offset
)
{
    Data encoded_name[11] = { 0 };
    match_result(Fat16Code, Fat16__encode_83_name(name, encoded_name),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    Data directory_sector[FAT16_BYTES_PER_SECTOR] = { 0 };

    for (uint16_t relative_sector = 0; relative_sector < self->root_dir_sectors; relative_sector++)
    {
        match_result(Fat16Code, Fat16Volume__read_sector(
            self,
            self->root_dir_start_sector + relative_sector,
            directory_sector
        ),
            success_do_nothing(),
            error(code, message) { return Error(Fat16Code, message, code); }
        );

        for (size_t entry_offset = 0; entry_offset < FAT16_BYTES_PER_SECTOR; entry_offset += FAT16_ROOT_ENTRY_SIZE)
        {
            if (directory_sector[entry_offset] == 0x00)
            {
                return Error(Fat16Code, not_found, FAT16_NOT_FOUND);
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
                *sector = self->root_dir_start_sector + relative_sector;
                *offset = entry_offset;
                return Success(Fat16Code, FAT16_OK);
            }
        }
    }

    return Error(Fat16Code, not_found, FAT16_NOT_FOUND);
}

static Result(Fat16Code) Fat16Volume__find_free_root_entry(
    Fat16VolumePtr self, BlockNumber * sector, size_t * offset
)
{
    Data directory_sector[FAT16_BYTES_PER_SECTOR] = { 0 };

    for (uint16_t relative_sector = 0; relative_sector < self->root_dir_sectors; relative_sector++)
    {
        match_result(Fat16Code, Fat16Volume__read_sector(
            self,
            self->root_dir_start_sector + relative_sector,
            directory_sector
        ),
            success_do_nothing(),
            error(code, message) { return Error(Fat16Code, message, code); }
        );

        for (size_t entry_offset = 0; entry_offset < FAT16_BYTES_PER_SECTOR; entry_offset += FAT16_ROOT_ENTRY_SIZE)
        {
            if (directory_sector[entry_offset] == 0x00 || directory_sector[entry_offset] == 0xe5)
            {
                *sector = self->root_dir_start_sector + relative_sector;
                *offset = entry_offset;
                return Success(Fat16Code, FAT16_OK);
            }
        }
    }

    return Error(Fat16Code, no_space, FAT16_NO_SPACE);
}

Result(Fat16Code) Fat16Volume__format(Fat16VolumePtr self)
{
    if (!Fat16Volume__has_valid_geometry(self))
    {
        return Error(Fat16Code, invalid_argument, FAT16_INVALID_ARGUMENT);
    }

    Data sector[FAT16_BYTES_PER_SECTOR] = { 0 };

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
    Fat16__write_u16(sector, 11, FAT16_BYTES_PER_SECTOR);
    sector[13] = FAT16_SECTORS_PER_CLUSTER;
    Fat16__write_u16(sector, 14, FAT16_RESERVED_SECTORS);
    sector[16] = FAT16_FAT_COUNT;
    Fat16__write_u16(sector, 17, FAT16_ROOT_ENTRY_COUNT);
    if (self->total_sectors <= 0xffff)
    {
        Fat16__write_u16(sector, 19, (uint16_t) self->total_sectors);
    }
    else
    {
        Fat16__write_u32(sector, 32, (uint32_t) self->total_sectors);
    }
    sector[21] = FAT16_MEDIA_FIXED_DISK;
    Fat16__write_u16(sector, 22, self->sectors_per_fat);
    Fat16__write_u16(sector, 24, 1);
    Fat16__write_u16(sector, 26, 1);
    sector[36] = 0x80;
    sector[38] = 0x29;
    Fat16__write_u32(sector, 39, 0x46415416);
    sector[43] = 'M';
    sector[44] = 'I';
    sector[45] = 'L';
    sector[46] = 'L';
    sector[47] = 'I';
    sector[48] = 'K';
    sector[49] = 'R';
    sector[50] = 'N';
    sector[51] = ' ';
    sector[52] = ' ';
    sector[53] = ' ';
    sector[54] = 'F';
    sector[55] = 'A';
    sector[56] = 'T';
    sector[57] = '1';
    sector[58] = '6';
    sector[59] = ' ';
    sector[60] = ' ';
    sector[61] = ' ';
    sector[510] = 0x55;
    sector[511] = 0xaa;

    match_result(Fat16Code, Fat16Volume__write_sector(self, 0, sector),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    for (uint8_t fat = 0; fat < FAT16_FAT_COUNT; fat++)
    {
        for (uint16_t i = 0; i < self->sectors_per_fat; i++)
        {
            Fat16__zero(sector, FAT16_BYTES_PER_SECTOR);
            if (i == 0)
            {
                Fat16__write_u16(sector, 0, 0xff00 | FAT16_MEDIA_FIXED_DISK);
                Fat16__write_u16(sector, 2, FAT16_END_OF_CHAIN);
            }

            match_result(Fat16Code, Fat16Volume__write_sector(
                self,
                self->fat_start_sector + (fat * self->sectors_per_fat) + i,
                sector
            ),
                success_do_nothing(),
                error(code, message) { return Error(Fat16Code, message, code); }
            );
        }
    }

    Fat16__zero(sector, FAT16_BYTES_PER_SECTOR);
    for (uint16_t i = 0; i < self->root_dir_sectors; i++)
    {
        match_result(Fat16Code, Fat16Volume__write_sector(self, self->root_dir_start_sector + i, sector),
            success_do_nothing(),
            error(code, message) { return Error(Fat16Code, message, code); }
        );
    }

    return Success(Fat16Code, FAT16_OK);
}

Result(Fat16Code) Fat16Volume__mount(Fat16VolumePtr self)
{
    Data sector[FAT16_BYTES_PER_SECTOR] = { 0 };
    match_result(Fat16Code, Fat16Volume__read_sector(self, 0, sector),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    if (sector[510] != 0x55
        || sector[511] != 0xaa
        || Fat16__read_u16(sector, 11) != FAT16_BYTES_PER_SECTOR
        || sector[13] != FAT16_SECTORS_PER_CLUSTER
        || sector[16] != FAT16_FAT_COUNT
        || Fat16__read_u16(sector, 17) != FAT16_ROOT_ENTRY_COUNT
        || Fat16__read_u16(sector, 22) == 0)
    {
        return Error(Fat16Code, invalid_volume, FAT16_INVALID_VOLUME);
    }

    BlockCount total_sectors = Fat16__read_u16(sector, 19);
    if (total_sectors == 0)
    {
        total_sectors = Fat16__read_u32(sector, 32);
    }

    self->total_sectors = total_sectors;
    self->sectors_per_fat = Fat16__read_u16(sector, 22);
    self->root_dir_sectors =
        (FAT16_ROOT_ENTRY_COUNT * FAT16_ROOT_ENTRY_SIZE + FAT16_BYTES_PER_SECTOR - 1) / FAT16_BYTES_PER_SECTOR;
    self->fat_start_sector = Fat16__read_u16(sector, 14);
    self->root_dir_start_sector = self->fat_start_sector + (FAT16_FAT_COUNT * self->sectors_per_fat);
    self->data_start_sector = self->root_dir_start_sector + self->root_dir_sectors;
    self->cluster_count = (uint16_t) ((self->total_sectors - self->data_start_sector) / FAT16_SECTORS_PER_CLUSTER);

    if (!Fat16Volume__has_valid_geometry(self))
    {
        return Error(Fat16Code, invalid_volume, FAT16_INVALID_VOLUME);
    }

    return Success(Fat16Code, FAT16_OK);
}

Result(Fat16DirectoryEntries) Fat16Volume__list_root(Fat16VolumePtr self)
{
    if (!Fat16Volume__has_valid_geometry(self))
    {
        return Error(Fat16DirectoryEntries, invalid_argument, FAT16_INVALID_ARGUMENT);
    }

    Fat16DirectoryEntries entries = Fat16DirectoryEntries__new();
    Data sector[FAT16_BYTES_PER_SECTOR] = { 0 };

    for (uint16_t relative_sector = 0; relative_sector < self->root_dir_sectors; relative_sector++)
    {
        match_result(Fat16Code, Fat16Volume__read_sector(self, self->root_dir_start_sector + relative_sector, sector),
            success_do_nothing(),
            error(code, message) { return Error(Fat16DirectoryEntries, message, code); }
        );

        for (size_t offset = 0; offset < FAT16_BYTES_PER_SECTOR; offset += FAT16_ROOT_ENTRY_SIZE)
        {
            if (sector[offset] == 0x00)
            {
                return Success(Fat16DirectoryEntries, entries);
            }

            if (sector[offset] == 0xe5 || sector[offset + 11] == 0x0f)
            {
                continue;
            }

            BString entry_name = Fat16__decode_83_name(&sector[offset]);
            Fat16DirectoryEntry entry = Fat16DirectoryEntry__new(
                &entry_name,
                sector[offset + 11],
                Fat16__read_u16(sector, offset + 26),
                Fat16__read_u32(sector, offset + 28)
            );

            match_result(CollectionCode, Fat16DirectoryEntries__push(&entries, entry),
                success_do_nothing(),
                error(code, message) { return Error(Fat16DirectoryEntries, message, code); }
            );
        }
    }

    return Success(Fat16DirectoryEntries, entries);
}

Result(Fat16Code) Fat16Volume__create_root_file(
    Fat16VolumePtr self, ConstBStringPtr name, DataPtr data, uint32_t size
)
{
    if (!Fat16Volume__has_valid_geometry(self))
    {
        return Error(Fat16Code, invalid_argument, FAT16_INVALID_ARGUMENT);
    }

    if (size > FAT16_MAX_FILE_SIZE)
    {
        return Error(Fat16Code, file_too_large, FAT16_FILE_TOO_LARGE);
    }

    Data encoded_name[11] = { 0 };
    match_result(Fat16Code, Fat16__encode_83_name(name, encoded_name),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    BlockNumber existing_sector = 0;
    size_t existing_offset = 0;
    match_result(Fat16Code, Fat16Volume__find_root_entry(self, name, &existing_sector, &existing_offset),
        success(_) { return Error(Fat16Code, already_exists, FAT16_ALREADY_EXISTS); },
        error(code, message)
        {
            if (code != FAT16_NOT_FOUND)
            {
                return Error(Fat16Code, message, code);
            }
        }
    );

    uint16_t cluster = 0;
    match_result(Fat16Code, Fat16Volume__find_free_cluster(self, &cluster),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    match_result(Fat16Code, Fat16Volume__write_fat_entry(self, cluster, FAT16_END_OF_CHAIN),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    Data file_sector[FAT16_BYTES_PER_SECTOR] = { 0 };
    if (size > 0)
    {
        Fat16__copy(file_sector, data, size);
    }

    match_result(Fat16Code, Fat16Volume__write_sector(self, Fat16Volume__cluster_sector(self, cluster), file_sector),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    BlockNumber entry_sector = 0;
    size_t entry_offset = 0;
    match_result(Fat16Code, Fat16Volume__find_free_root_entry(self, &entry_sector, &entry_offset),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    Data directory_sector[FAT16_BYTES_PER_SECTOR] = { 0 };
    match_result(Fat16Code, Fat16Volume__read_sector(self, entry_sector, directory_sector),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    Fat16__copy(&directory_sector[entry_offset], encoded_name, 11);
    directory_sector[entry_offset + 11] = FAT16_ATTR_ARCHIVE;
    Fat16__write_u16(directory_sector, entry_offset + 26, cluster);
    Fat16__write_u32(directory_sector, entry_offset + 28, size);

    match_result(Fat16Code, Fat16Volume__write_sector(self, entry_sector, directory_sector),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    return Success(Fat16Code, FAT16_OK);
}

Result(Fat16Code) Fat16Volume__create_root_directory(Fat16VolumePtr self, ConstBStringPtr name)
{
    if (!Fat16Volume__has_valid_geometry(self))
    {
        return Error(Fat16Code, invalid_argument, FAT16_INVALID_ARGUMENT);
    }

    Data encoded_name[11] = { 0 };
    match_result(Fat16Code, Fat16__encode_83_name(name, encoded_name),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    BlockNumber existing_sector = 0;
    size_t existing_offset = 0;
    match_result(Fat16Code, Fat16Volume__find_root_entry(self, name, &existing_sector, &existing_offset),
        success(_) { return Error(Fat16Code, already_exists, FAT16_ALREADY_EXISTS); },
        error(code, message)
        {
            if (code != FAT16_NOT_FOUND)
            {
                return Error(Fat16Code, message, code);
            }
        }
    );

    uint16_t cluster = 0;
    match_result(Fat16Code, Fat16Volume__find_free_cluster(self, &cluster),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    match_result(Fat16Code, Fat16Volume__write_fat_entry(self, cluster, FAT16_END_OF_CHAIN),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    Data directory_data[FAT16_BYTES_PER_SECTOR] = { 0 };
    match_result(Fat16Code, Fat16Volume__write_sector(self, Fat16Volume__cluster_sector(self, cluster), directory_data),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    BlockNumber entry_sector = 0;
    size_t entry_offset = 0;
    match_result(Fat16Code, Fat16Volume__find_free_root_entry(self, &entry_sector, &entry_offset),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    Data directory_sector[FAT16_BYTES_PER_SECTOR] = { 0 };
    match_result(Fat16Code, Fat16Volume__read_sector(self, entry_sector, directory_sector),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    Fat16__copy(&directory_sector[entry_offset], encoded_name, 11);
    directory_sector[entry_offset + 11] = FAT16_ATTR_DIRECTORY;
    Fat16__write_u16(directory_sector, entry_offset + 26, cluster);
    Fat16__write_u32(directory_sector, entry_offset + 28, 0);

    match_result(Fat16Code, Fat16Volume__write_sector(self, entry_sector, directory_sector),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    return Success(Fat16Code, FAT16_OK);
}

Result(Fat16Code) Fat16Volume__read_root_file(
    Fat16VolumePtr self, ConstBStringPtr name, DataPtr target, uint32_t target_size, uint32_t * bytes_read
)
{
    if (!Fat16Volume__has_valid_geometry(self))
    {
        return Error(Fat16Code, invalid_argument, FAT16_INVALID_ARGUMENT);
    }

    BlockNumber entry_sector = 0;
    size_t entry_offset = 0;
    match_result(Fat16Code, Fat16Volume__find_root_entry(self, name, &entry_sector, &entry_offset),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    Data directory_sector[FAT16_BYTES_PER_SECTOR] = { 0 };
    match_result(Fat16Code, Fat16Volume__read_sector(self, entry_sector, directory_sector),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    uint16_t cluster = Fat16__read_u16(directory_sector, entry_offset + 26);
    uint32_t size = Fat16__read_u32(directory_sector, entry_offset + 28);
    if (size > target_size || size > FAT16_MAX_FILE_SIZE || cluster < 2)
    {
        return Error(Fat16Code, invalid_argument, FAT16_INVALID_ARGUMENT);
    }

    Data file_sector[FAT16_BYTES_PER_SECTOR] = { 0 };
    match_result(Fat16Code, Fat16Volume__read_sector(self, Fat16Volume__cluster_sector(self, cluster), file_sector),
        success_do_nothing(),
        error(code, message) { return Error(Fat16Code, message, code); }
    );

    Fat16__copy(target, file_sector, size);
    *bytes_read = size;
    return Success(Fat16Code, FAT16_OK);
}

static IOStatus Fat16FilesystemIO__status_from_fat16(Fat16Code code)
{
    if (code == FAT16_NOT_FOUND || code == FAT16_UNSUPPORTED_NAME || code == FAT16_INVALID_ARGUMENT)
    {
        return IO_INVALID_PATH;
    }

    if (code == FAT16_IO_ERROR || code == FAT16_INVALID_VOLUME)
    {
        return IO_NO_DEVICE_FOUND;
    }

    if (code == FAT16_NO_SPACE || code == FAT16_ALREADY_EXISTS || code == FAT16_FILE_TOO_LARGE)
    {
        return IO_OPERATION_NOT_SUPPORTED;
    }

    return IO_OPERATION_NOT_SUPPORTED;
}

static bool Fat16FilesystemIO__is_root_path(ConstBStringPtr path)
{
    return BString__is_empty(path)
        || (BString__length(path) == 1 && BString__char_at_or(path, 0, '\0') == '/');
}

static bool Fat16FilesystemIO__is_root_file_path(ConstBStringPtr path)
{
    if (BString__is_empty(path))
    {
        return false;
    }

    for (StringPos i = 0; i < BString__length(path); i++)
    {
        if (BString__char_at_or(path, i, '\0') == '/')
        {
            return false;
        }
    }

    return true;
}

static Fat16FileHandlePtr Fat16FileHandle__heap_new(ConstBStringPtr path, Flags flags)
{
    Fat16FileHandlePtr handle = ptr(Kernel__alloc_kernel(sizeof(Fat16FileHandle)), Fat16FileHandle);
    *handle = (Fat16FileHandle) {
        .path = *path,
        .offset = 0,
        .buffer = EmptyRingBuffer(IOBuffer),
        .flags = flags,
    };

    return handle;
}

static Result(IOInternalHandle) Fat16FileIO__open(IOStatePtr self, ConstBStringPtr path, Flags flags)
{
    Fat16FilesystemIOStatePtr fs_state = (Fat16FilesystemIOStatePtr) self;
    BString open_path = *path;

    if (!fs_state->mounted || !Fat16FilesystemIO__is_root_file_path(&open_path))
    {
        return Error(IOInternalHandle, invalid_path, IO_INVALID_PATH);
    }

    if ((flags & IO_OPEN_READ) != 0)
    {
        Data data[FAT16_MAX_FILE_SIZE] = { 0 };
        uint32_t bytes_read = 0;
        match_result(Fat16Code, Fat16Volume__read_root_file(&fs_state->volume, &open_path, data, sizeof(data), &bytes_read),
            success_do_nothing(),
            error(code, message)
            {
                return Error(
                IOInternalHandle,
                message,
                Fat16FilesystemIO__status_from_fat16(code)
            ); }
        );
    }

    critical_section(fs_state->state.lock,
    {
        IOInternalHandle handle = (IOInternalHandle) fs_state->handle_count++ | fs_state->state.filesystem->fs_code;
        Fat16FileHandleMap__set(&fs_state->handles, handle, Fat16FileHandle__heap_new(&open_path, flags));
        cs_return Success(IOInternalHandle, handle);
    });
}

static Result(IOInternalHandle) Fat16FileIO__close(IOStatePtr self, IOInternalHandle handle)
{
    Fat16FilesystemIOStatePtr fs_state = (Fat16FilesystemIOStatePtr) self;

    critical_section(fs_state->state.lock,
    {
        match_optional(Fat16FileHandlePtr, Fat16FileHandleMap__get(&fs_state->handles, handle),
            some(_)
            {
                Fat16FileHandleMap__delete(&fs_state->handles, handle);
                cs_return Success(IOInternalHandle, handle);
            },
            none() { cs_return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOBufferPtr) Fat16FileIO__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ConstExternalBufferPtr target_buffer
)
{
    Fat16FilesystemIOStatePtr fs_state = (Fat16FilesystemIOStatePtr) self;

    critical_section(fs_state->state.lock,
    {
        match_optional(Fat16FileHandlePtr, Fat16FileHandleMap__get(&fs_state->handles, handle),
            some(target)
            {
                if ((target->flags & IO_OPEN_READ) == 0)
                {
                    cs_return Error(IOBufferPtr, invalid_handle, IO_OPERATION_NOT_SUPPORTED);
                }

                Data data[FAT16_MAX_FILE_SIZE] = { 0 };
                uint32_t bytes_read = 0;
                match_result(Fat16Code, Fat16Volume__read_root_file(
                    &fs_state->volume, &target->path, data, sizeof(data), &bytes_read
                ),
                    success_do_nothing(),
                    error(code, message)
                    {
                        cs_return Error(
                        IOBufferPtr,
                        message,
                        Fat16FilesystemIO__status_from_fat16(code)
                    ); }
                );

                if (target->offset >= bytes_read)
                {
                    cs_return Error(IOBufferPtr, not_found, IO_EOF);
                }

                size_t remaining = bytes_read - target->offset;
                size_t free_space = IOBuffer__free_space(&target->buffer);
                size_t read_size = Fat16__min_size(size, Fat16__min_size(remaining, free_space));
                IOBuffer__push_all(&target->buffer, &data[target->offset], read_size);
                target->offset += read_size;

                cs_return Success(IOBufferPtr, &target->buffer);
            },
            none() { cs_return Error(IOBufferPtr, invalid_handle, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(IOBufferPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(ByteCount) Fat16FileIO__write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size)
{
    Fat16FilesystemIOStatePtr fs_state = (Fat16FilesystemIOStatePtr) self;

    critical_section(fs_state->state.lock,
    {
        match_optional(Fat16FileHandlePtr, Fat16FileHandleMap__get(&fs_state->handles, handle),
            some(target)
            {
                if ((target->flags & IO_OPEN_WRITE) == 0 || target->offset != 0)
                {
                    cs_return Error(ByteCount, invalid_handle, IO_OPERATION_NOT_SUPPORTED);
                }

                match_result(Fat16Code, Fat16Volume__create_root_file(&fs_state->volume, &target->path, data, size),
                    success(_)
                    {
                        target->offset = size;
                        cs_return Success(ByteCount, size);
                    },
                    error(code, message)
                    {
                        cs_return Error(
                        ByteCount,
                        message,
                        Fat16FilesystemIO__status_from_fat16(code)
                    ); }
                );
            },
            none() { cs_return Error(ByteCount, invalid_handle, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(ByteCount, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(ParkStatus) Fat16FileIO__park(IOStatePtr self, IOContextPtr parked_context)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

static Result(ParkStatus) Fat16FileIO__unpark(IOStatePtr self)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

static Result(FilesystemNodes) Fat16FileIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, ConstBStringPtr path)
{
    Fat16FilesystemIOStatePtr fs_state = (Fat16FilesystemIOStatePtr) self;
    BString list_path = *path;
    FilesystemNodes result = EmptyArray(FilesystemNodes);

    if (!fs_state->mounted)
    {
        return Error(FilesystemNodes, invalid_path, IO_INVALID_PATH);
    }

    if (!Fat16FilesystemIO__is_root_path(&list_path))
    {
        if (!Fat16FilesystemIO__is_root_file_path(&list_path))
        {
            return Error(FilesystemNodes, invalid_path, IO_INVALID_PATH);
        }

        BlockNumber entry_sector = 0;
        size_t entry_offset = 0;
        match_result(Fat16Code, Fat16Volume__find_root_entry(&fs_state->volume, &list_path, &entry_sector, &entry_offset),
            success_do_nothing(),
            error(code, message)
            {
                return Error(
                FilesystemNodes,
                message,
                Fat16FilesystemIO__status_from_fat16(code)
            ); }
        );

        Data directory_sector[FAT16_BYTES_PER_SECTOR] = { 0 };
        match_result(Fat16Code, Fat16Volume__read_sector(&fs_state->volume, entry_sector, directory_sector),
            success_do_nothing(),
            error(code, message)
            {
                return Error(
                FilesystemNodes,
                message,
                Fat16FilesystemIO__status_from_fat16(code)
            ); }
        );

        if ((directory_sector[entry_offset + 11] & FAT16_ATTR_DIRECTORY) == 0)
        {
            return Error(FilesystemNodes, invalid_path, IO_INVALID_PATH);
        }

        return Success(FilesystemNodes, result);
    }

    match_result(Fat16DirectoryEntries, Fat16Volume__list_root(&fs_state->volume),
        success(entries)
        {
            for_each_fa(Fat16DirectoryEntries, entries, index, entry,
            {
                FilesystemNodes__push(
                    &result,
                    FilesystemNode__heap_new(fs_state->state.filesystem, &entry.name, entry.attributes)
                );
            });
            return Success(FilesystemNodes, result);
        },
        error(code, message)
        {
            return Error(
            FilesystemNodes,
            message,
            Fat16FilesystemIO__status_from_fat16(code)
        ); }
    );

    return Success(FilesystemNodes, result);
}

static Result(IOStatus) Fat16FileIO__mkdir(IOStatePtr self, ConstBStringPtr path)
{
    Fat16FilesystemIOStatePtr fs_state = (Fat16FilesystemIOStatePtr) self;
    BString mkdir_path = *path;
    if (!fs_state->mounted || !Fat16FilesystemIO__is_root_file_path(&mkdir_path))
    {
        return Error(IOStatus, invalid_path, IO_INVALID_PATH);
    }

    match_result(Fat16Code, Fat16Volume__create_root_directory(&fs_state->volume, &mkdir_path),
        success(_) { return Success(IOStatus, IO_OK); },
        error(code, message) { return Error(IOStatus, message, Fat16FilesystemIO__status_from_fat16(code)); }
    );

    return Error(IOStatus, invalid_path, IO_INVALID_PATH);
}

FileIO Fat16FilesystemIO__new()
{
    return (FileIO) {
        .base_io = IO__new(
            Fat16FileIO__open,
            Fat16FileIO__read,
            Fat16FileIO__write,
            IO__unsupported_accept,
            Fat16FileIO__close,
            Fat16FileIO__park,
            Fat16FileIO__unpark,
            IO__unsupported_server_buffer,
            IO__unsupported_server_write_buffer,
            IO__unsupported_data_available,
            IO__unsupported_data_consumed,
            IO__unsupported_eof
        ),
        .list_nodes = Fat16FileIO__list_nodes,
        .list_dir = FileIO__unsupported_list_dir,
        .mkdir = Fat16FileIO__mkdir,
        .remove = FileIO__unsupported_remove,
        .link = FileIO__unsupported_link,
    };
}

bool Fat16FilesystemIO__available()
{
    return true;
}

Fat16FilesystemIOStatePtr Fat16FilesystemIOState__heap_new(
    FilesystemPtr fs, BlockDevicePtr device, BlockCount total_sectors
)
{
    Fat16FilesystemIOStatePtr io_state = ptr(
        Kernel__alloc_kernel(sizeof(Fat16FilesystemIOState)), Fat16FilesystemIOState
    );

    if (total_sectors == 0)
    {
        total_sectors = FAT16_DEFAULT_MEMORY_SECTORS;
    }

    *io_state = (Fat16FilesystemIOState) {
        .state = FilesystemIOState__new(fs),
        .handles = Fat16FileHandleMap__new(),
        .handle_count = 0,
        .mounted = false,
    };

    io_state->volume = Fat16Volume__new(device, total_sectors);

    match_result(Fat16Code, Fat16Volume__format(&io_state->volume),
        success(_)
        {
            match_result(Fat16Code, Fat16Volume__mount(&io_state->volume),
                success(_) { io_state->mounted = true; },
                error(code, message)
                {
                    Kernel__panic(BString__cstr(&message), NULL); // hundelint: ignore-no-null
                }
            );
        },
        error(code, message)
        {
            Kernel__panic(BString__cstr(&message), NULL); // hundelint: ignore-no-null
        }
    );

    fs->state = &io_state->state;

    return io_state;
}
