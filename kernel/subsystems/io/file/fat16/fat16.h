#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_FAT16_FAT16_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_FAT16_FAT16_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../block/block_device.h"
#include "../filesystem.h"
#include "../../../../types/fixed_array.h"
#include "../../../../types/bstring.h"
#include "../../../../types/map.h"
#include "../../../../types/result.h"

#define FAT16_BYTES_PER_SECTOR BLOCK_DEVICE_BLOCK_SIZE
#define FAT16_SECTORS_PER_CLUSTER 1
#define FAT16_RESERVED_SECTORS 1
#define FAT16_FAT_COUNT 2
#define FAT16_ROOT_ENTRY_COUNT 64
#define FAT16_ROOT_ENTRY_SIZE 32
#define FAT16_MEDIA_FIXED_DISK 0xf8
#define FAT16_END_OF_CHAIN 0xffff
#define FAT16_EMPTY_CLUSTER 0x0000
#define FAT16_ATTR_DIRECTORY 0x10
#define FAT16_ATTR_ARCHIVE 0x20
#define FAT16_MAX_ROOT_ENTRIES FAT16_ROOT_ENTRY_COUNT
#define FAT16_MAX_FILE_SIZE (FAT16_BYTES_PER_SECTOR * FAT16_SECTORS_PER_CLUSTER)

typedef enum Fat16Code
{
    FAT16_OK = 0,
    FAT16_INVALID_ARGUMENT = -1,
    FAT16_IO_ERROR = -2,
    FAT16_INVALID_VOLUME = -3,
    FAT16_NO_SPACE = -4,
    FAT16_NOT_FOUND = -5,
    FAT16_ALREADY_EXISTS = -6,
    FAT16_UNSUPPORTED_NAME = -7,
    FAT16_FILE_TOO_LARGE = -8,
}
Fat16Code;

ResultDef(Fat16Code);

typedef struct Fat16Volume
{
    BlockDevicePtr device;
    BlockCount total_sectors;
    uint16_t sectors_per_fat;
    uint16_t root_dir_sectors;
    BlockNumber fat_start_sector;
    BlockNumber root_dir_start_sector;
    BlockNumber data_start_sector;
    uint16_t cluster_count;
}
Fat16Volume;

typedef Fat16Volume * Fat16VolumePtr;

typedef struct Fat16DirectoryEntry
{
    BString name;
    uint8_t attributes;
    uint16_t first_cluster;
    uint32_t size;
}
Fat16DirectoryEntry;

typedef Fat16DirectoryEntry * Fat16DirectoryEntryPtr;

OptionalDef(Fat16DirectoryEntry);
bool Fat16DirectoryEntry__equals(Fat16DirectoryEntryPtr self, Fat16DirectoryEntryPtr other);
FixedArrayDefine(
    Fat16DirectoryEntry,
    FAT16_MAX_ROOT_ENTRIES,
    Fat16DirectoryEntries,
    (EqualFunction) Fat16DirectoryEntry__equals
);
ResultDef(Fat16DirectoryEntries);

typedef struct Fat16FileHandle
{
    BString path;
    FilePointer offset;
    IOBuffer buffer;
    Flags flags;
}
Fat16FileHandle;

typedef Fat16FileHandle * Fat16FileHandlePtr;

OptionalDef(Fat16FileHandlePtr);

MapDefine(IOInternalHandle, Fat16FileHandlePtr, MAX_HANDLES_PER_DEVICE, Fat16FileHandleMap, SimpleEquals(uint32_t));

typedef struct Fat16FilesystemIOState
{
    FilesystemIOState state;
    Fat16Volume volume;
    Fat16FileHandleMap handles;
    uint32_t handle_count;
    bool mounted;
}
Fat16FilesystemIOState;

typedef Fat16FilesystemIOState * Fat16FilesystemIOStatePtr;

Fat16Volume Fat16Volume__new(BlockDevicePtr device, BlockCount total_sectors);
Result(Fat16Code) Fat16Volume__format(Fat16VolumePtr self);
Result(Fat16Code) Fat16Volume__mount(Fat16VolumePtr self);
Result(Fat16DirectoryEntries) Fat16Volume__list_root(Fat16VolumePtr self);
Result(Fat16Code) Fat16Volume__create_root_file(
    Fat16VolumePtr self, BString name, DataPtr data, uint32_t size
);
Result(Fat16Code) Fat16Volume__create_root_directory(Fat16VolumePtr self, BString name);
Result(Fat16Code) Fat16Volume__read_root_file(
    Fat16VolumePtr self, BString name, DataPtr target, uint32_t target_size, uint32_t * bytes_read
);

FileIO Fat16FilesystemIO__new();
bool Fat16FilesystemIO__available();
Fat16FilesystemIOStatePtr Fat16FilesystemIOState__heap_new(
    FilesystemPtr fs, BlockDevicePtr device, BlockCount total_sectors
);

#endif
