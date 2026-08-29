#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_FILESYSTEM_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_FILESYSTEM_H

#include <stdint.h>
#include "../../../all_defs.h"
#include "../../../types/linked_list.h"
#include "../../../types/map.h"
#include "../../../types/bstring_additional_types.h"

#include "file_io.h"

typedef int64_t MountResult;
typedef uint64_t FilesystemCode;

typedef enum MountResults
{
    MOUNT_OK = 0,
    MOUNT_ERROR = -1,
}
MountResults;

ResultDef(MountResult);

typedef struct Filesystem Filesystem;
typedef Filesystem * FilesystemPtr;
typedef uint64_t FilePointer;

typedef struct FilesystemTableEntry
{
    BString device_path;
    BString filesystem_type;
    BString mountpoint;
    bool match_block_count;
    uint64_t block_count;
}
FilesystemTableEntry;

typedef FilesystemTableEntry * FilesystemTableEntryPtr;

OptionalDef(FilesystemTableEntry);
bool FilesystemTableEntry__equals(FilesystemTableEntryPtr self, FilesystemTableEntryPtr other);
FilesystemTableEntry FilesystemTableEntry__new(
    BString device_path,
    BString filesystem_type,
    BString mountpoint,
    bool match_block_count,
    uint64_t block_count
);
LinkedListDefine(FilesystemTableEntry, FilesystemTableEntries, (EqualFunction) FilesystemTableEntry__equals);

typedef struct FilesystemTable
{
    FilesystemTableEntries entries;
}
FilesystemTable;

typedef FilesystemTable * FilesystemTablePtr;

FilesystemTable FilesystemTable__new();
Result(CollectionCode) FilesystemTable__add(
    FilesystemTablePtr self, BString device_path, BString filesystem_type, BString mountpoint
);
Result(CollectionCode) FilesystemTable__add_by_block_count(
    FilesystemTablePtr self, uint64_t block_count, BString filesystem_type, BString mountpoint
);

OptionalDef(FilesystemPtr);
ResultDef(FilesystemPtr);
MapDefine(BString, FilesystemPtr, MAX_MOUNTS, Mounts, bstring_equals);

typedef struct FilesystemNode FilesystemNode;
typedef FilesystemNode * FilesystemNodePtr;

typedef struct FilesystemIOState FilesystemIOState;
typedef FilesystemIOState * FilesystemIOStatePtr;

typedef struct Filesystem
{
    BString name;
    Mounts mounts;
    FilesystemTable filesystem_table;
    FilesystemNodePtr root;
    FileIO file_io;
    FilesystemIOStatePtr state;
    FilesystemCode fs_code;
}
Filesystem;

typedef struct FileHandleTarget
{
    BString path;
    FilePointer offset;
    IOBuffer buffer;
}
FileHandleTarget;

typedef FileHandleTarget * FileHandleTargetPtr;

OptionalDef(FileHandleTarget);
OptionalDef(FileHandleTargetPtr);
ResultDef(FileHandleTargetPtr);

MapDefine(IOInternalHandle, FileHandleTargetPtr, MAX_HANDLES_PER_DEVICE, FileHandleTargetMap, SimpleEquals(uint32_t));


typedef struct FilesystemIOState
{
    // inheritance
    IOState state;
    FilesystemPtr filesystem;
    FileHandleTargetMap handles;

    mutex_t lock;
}
FilesystemIOState;

typedef FilesystemIOState * FilesystemIOStatePtr;

FileHandleTargetPtr FileHandleTarget__heap_new(BString path);
void FileHandleTarget__dispose(FileHandleTargetPtr self);

FilesystemPtr Filesystem__heap_new(BString name, FileIO file_io);
FilesystemPtr RootFilesystem__heap_new();

Result(MountResult) Filesystem__mount(FilesystemPtr root, BString path, FilesystemPtr filesystem);

FilesystemIOState FilesystemIOState__new(FilesystemPtr filesystem);
FilesystemIOStatePtr FilesystemIOState__heap_new(FilesystemPtr filesystem);

#endif
