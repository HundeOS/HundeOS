#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_FAT32_FAT32_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_FAT32_FAT32_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../block/block_device.h"
#include "../filesystem.h"
#include "../../../../types/fixed_array.h"
#include "../../../../types/bstring.h"
#include "../../../../types/map.h"
#include "../../../../types/result.h"

#define FAT32_BYTES_PER_SECTOR BLOCK_DEVICE_BLOCK_SIZE
#define FAT32_SECTORS_PER_CLUSTER 1
#define FAT32_RESERVED_SECTORS 32
#define FAT32_FAT_COUNT 2
#define FAT32_ROOT_ENTRY_SIZE 32
#define FAT32_MEDIA_FIXED_DISK 0xf8
#define FAT32_END_OF_CHAIN 0x0fffffff
#define FAT32_EMPTY_CLUSTER 0x00000000
#define FAT32_ROOT_CLUSTER 2
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_LFN 0x0f
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE 0x20
#define FAT32_SHORT_NAME_SIZE 11
#define FAT32_MAX_ROOT_ENTRIES 128
#define FAT32_MAX_FILE_SIZE (FAT32_BYTES_PER_SECTOR * FAT32_SECTORS_PER_CLUSTER)
#define FAT32_MAX_READ_SIZE (FAT32_BYTES_PER_SECTOR * 128)

typedef uint8_t Fat32Attribute;
typedef Fat32Attribute * Fat32AttributePtr;
typedef uint8_t Fat32Checksum;
typedef Fat32Checksum * Fat32ChecksumPtr;
typedef uint32_t Fat32Cluster;
typedef Fat32Cluster * Fat32ClusterPtr;
typedef bool Fat32Flag;
typedef Fat32Flag * Fat32FlagPtr;
typedef size_t Fat32Offset;
typedef Fat32Offset * Fat32OffsetPtr;
typedef BlockNumber * BlockNumberPtr;

OptionalDef(Data);
FixedArrayDefine(Data, FAT32_SHORT_NAME_SIZE, Fat32ShortName, SimpleEquals(uint8_t));
typedef Fat32ShortName * Fat32ShortNamePtr;

typedef enum Fat32Code
{
    FAT32_OK = 0,
    FAT32_INVALID_ARGUMENT = -1,
    FAT32_IO_ERROR = -2,
    FAT32_INVALID_VOLUME = -3,
    FAT32_NO_SPACE = -4,
    FAT32_NOT_FOUND = -5,
    FAT32_ALREADY_EXISTS = -6,
    FAT32_UNSUPPORTED_NAME = -7,
    FAT32_FILE_TOO_LARGE = -8,
}
Fat32Code;

ResultDef(Fat32Code);

typedef struct Fat32Volume
{
    Optional(BlockDevicePtr) device;
    BlockCount total_sectors;
    uint32_t sectors_per_fat;
    BlockNumber fat_start_sector;
    BlockNumber data_start_sector;
    uint32_t root_cluster;
    uint32_t cluster_count;
}
Fat32Volume;

typedef Fat32Volume * Fat32VolumePtr;

typedef struct Fat32DirectoryEntry
{
    BString name;
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t size;
}
Fat32DirectoryEntry;

typedef Fat32DirectoryEntry * Fat32DirectoryEntryPtr;

OptionalDef(Fat32DirectoryEntry);
ResultDef(Fat32DirectoryEntry);
bool Fat32DirectoryEntry__equals(Fat32DirectoryEntryPtr self, Fat32DirectoryEntryPtr other);
Fat32DirectoryEntry Fat32DirectoryEntry__new(
    BString name,
    uint8_t attributes,
    uint32_t first_cluster,
    uint32_t size
);
FixedArrayDefine(
    Fat32DirectoryEntry,
    FAT32_MAX_ROOT_ENTRIES,
    Fat32DirectoryEntries,
    (EqualFunction) Fat32DirectoryEntry__equals
);
ResultDef(Fat32DirectoryEntries);

typedef struct Fat32FileHandle
{
    BString path;
    FilePointer offset;
    IOBuffer buffer;
    Flags flags;
}
Fat32FileHandle;

typedef Fat32FileHandle * Fat32FileHandlePtr;

OptionalDef(Fat32FileHandlePtr);
ResultDef(Fat32FileHandlePtr);

MapDefine(IOInternalHandle, Fat32FileHandlePtr, MAX_HANDLES_PER_DEVICE, Fat32FileHandleMap, SimpleEquals(uint32_t));

typedef struct Fat32FilesystemIOState
{
    FilesystemIOState state;
    Fat32Volume volume;
    Fat32FileHandleMap handles;
    uint32_t handle_count;
    bool mounted;
}
Fat32FilesystemIOState;

typedef Fat32FilesystemIOState * Fat32FilesystemIOStatePtr;

Fat32Volume Fat32Volume__new(Optional(BlockDevicePtr) device, BlockCount total_sectors);
Result(Fat32Code) Fat32Volume__format(Fat32VolumePtr self);
Result(Fat32Code) Fat32Volume__mount(Fat32VolumePtr self);
Result(Fat32DirectoryEntries) Fat32Volume__list_root(Fat32VolumePtr self);
Result(Fat32Code) Fat32Volume__create_root_file(
    Fat32VolumePtr self, BString name, DataPtr data, uint32_t size
);
Result(Fat32Code) Fat32Volume__create_root_directory(Fat32VolumePtr self, BString name);
Result(Fat32Code) Fat32Volume__read_root_file(
    Fat32VolumePtr self, BString name, DataPtr target, uint32_t target_size, Fat32ClusterPtr bytes_read
);

FileIO Fat32FilesystemIO__new();
bool Fat32FilesystemIO__available();
Fat32FilesystemIOStatePtr Fat32FilesystemIOState__heap_new(
    FilesystemPtr fs, BlockDevicePtr device, BlockCount total_sectors
);

#endif
