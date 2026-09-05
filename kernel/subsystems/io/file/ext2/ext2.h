#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_EXT2_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_EXT2_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../block/block_device.h"
#include "../filesystem.h"
#include "../../../../types/fixed_array.h"
#include "../../../../types/bstring.h"
#include "../../../../types/map.h"
#include "../../../../types/result.h"

#define EXT2_SUPERBLOCK_OFFSET 1024U
#define EXT2_SUPERBLOCK_SECTOR 2U
#define EXT2_BLOCK_SIZE 1024U
#define EXT2_BLOCK_SECTORS (EXT2_BLOCK_SIZE / BLOCK_DEVICE_BLOCK_SIZE)
#define EXT2_SUPER_MAGIC 0xEF53U
#define EXT2_ROOT_INODE 2U
#define EXT2_NDIR_BLOCKS 12U
#define EXT2_MAX_READ_SIZE (EXT2_BLOCK_SIZE * EXT2_NDIR_BLOCKS)
#define EXT2_MAX_ROOT_ENTRIES 128U
#define EXT2_MAX_GROUPS 32U

#define EXT2_INODE_MODE_FILE 0x8000U
#define EXT2_INODE_MODE_DIR 0x4000U

typedef uint16_t Ext2InodeMode;
typedef uint32_t Ext2Block;
typedef uint32_t Ext2BlockCount;
typedef uint32_t Ext2InodeNumber;
typedef uint32_t Ext2InodeCount;
typedef uint32_t Ext2InodeSize;
typedef uint32_t Ext2FileSize;
typedef uint32_t Ext2HandleCount;
typedef uint8_t Ext2DirectoryEntryType;

typedef enum Ext2DirectoryEntryTypes
{
    EXT2_DIRENT_TYPE_UNKNOWN = 0,
    EXT2_DIRENT_TYPE_FILE = 1,
    EXT2_DIRENT_TYPE_DIR = 2,
    EXT2_DIRENT_TYPE_CHAR_DEVICE = 3,
    EXT2_DIRENT_TYPE_BLOCK_DEVICE = 4,
    EXT2_DIRENT_TYPE_FIFO = 5,
    EXT2_DIRENT_TYPE_SOCKET = 6,
    EXT2_DIRENT_TYPE_SYMLINK = 7,
}
Ext2DirectoryEntryTypes;

typedef enum Ext2Code
{
    EXT2_OK = 0,
    EXT2_INVALID_ARGUMENT = -1,
    EXT2_IO_ERROR = -2,
    EXT2_INVALID_VOLUME = -3,
    EXT2_NOT_FOUND = -4,
    EXT2_NOT_SUPPORTED = -5,
    EXT2_FILE_TOO_LARGE = -6,
}
Ext2Code;

ResultDef(Ext2Code);

typedef struct Ext2Volume
{
    Optional(BlockDevicePtr) device;
    BlockCount total_sectors;
    Ext2BlockCount blocks_count;
    Ext2BlockCount blocks_per_group;
    Ext2InodeCount inodes_per_group;
    Ext2InodeSize inode_size;
    Ext2BlockCount group_count;
    Ext2Block block_bitmap_block;
    Ext2Block inode_bitmap_block;
    Ext2Block inode_table_block;
    Ext2Block group_block_bitmap_block[EXT2_MAX_GROUPS];
    Ext2Block group_inode_bitmap_block[EXT2_MAX_GROUPS];
    Ext2Block group_inode_table_block[EXT2_MAX_GROUPS];
    Ext2Block first_data_block;
    bool mounted;
}
Ext2Volume;

typedef Ext2Volume * Ext2VolumePtr;

typedef struct Ext2DirectoryEntry
{
    BString name;
    Ext2DirectoryEntryType file_type;
    Ext2InodeNumber inode;
    Ext2InodeMode mode;
    Ext2FileSize size;
}
Ext2DirectoryEntry;

typedef Ext2DirectoryEntry * Ext2DirectoryEntryPtr;

OptionalDef(Ext2DirectoryEntry);
bool Ext2DirectoryEntry__equals(Ext2DirectoryEntryPtr self, Ext2DirectoryEntryPtr other);
Ext2DirectoryEntry Ext2DirectoryEntry__new(
    ConstBStringPtr name,
    Ext2DirectoryEntryType file_type,
    Ext2InodeNumber inode,
    Ext2InodeMode mode,
    Ext2FileSize size
);
FixedArrayDefine(
    Ext2DirectoryEntry,
    EXT2_MAX_ROOT_ENTRIES,
    Ext2DirectoryEntries,
    (EqualFunction) Ext2DirectoryEntry__equals
);
ResultDef(Ext2DirectoryEntries);

typedef struct Ext2FileHandle
{
    BString path;
    FilePointer offset;
    Ext2InodeNumber inode_number;
    Ext2InodeMode mode;
    Ext2FileSize size;
    Ext2Block block[EXT2_NDIR_BLOCKS];
    IOBuffer buffer;
    Flags flags;
}
Ext2FileHandle;

typedef Ext2FileHandle * Ext2FileHandlePtr;

OptionalDef(Ext2FileHandlePtr);
ResultDef(Ext2FileHandlePtr);
MapDefine(IOInternalHandle, Ext2FileHandlePtr, MAX_HANDLES_PER_DEVICE, Ext2FileHandleMap, SimpleEquals(uint32_t));

typedef struct Ext2FilesystemIOState
{
    FilesystemIOState state;
    Ext2Volume volume;
    Ext2FileHandleMap handles;
    Ext2HandleCount handle_count;
}
Ext2FilesystemIOState;

typedef Ext2FilesystemIOState * Ext2FilesystemIOStatePtr;

Ext2Volume Ext2Volume__new(OptionalPtr(BlockDevicePtr) device, BlockCount total_sectors);
Result(Ext2Code) Ext2Volume__mount(Ext2VolumePtr self);
Result(Ext2DirectoryEntries) Ext2Volume__list_root(Ext2VolumePtr self);

FileIO Ext2FilesystemIO__new();
bool Ext2FilesystemIO__available();
Ext2FilesystemIOStatePtr Ext2FilesystemIOState__heap_new(
    FilesystemPtr fs, BlockDevicePtr device, BlockCount total_sectors
);

#endif
