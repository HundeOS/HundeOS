#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_FILESYSTEM_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_FILESYSTEM_H

#include <stdint.h>
#include "../../../all_defs.h"
#include "../../../types/linked_list.h"
#include "../../../types/map.h"
#include "../../../types/kstring_additional_types.h"

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
    KString device_path;
    KString filesystem_type;
    KString mountpoint;
}
FilesystemTableEntry;

typedef FilesystemTableEntry * FilesystemTableEntryPtr;

OptionalDef(FilesystemTableEntry);
bool FilesystemTableEntry__equals(FilesystemTableEntryPtr self, FilesystemTableEntryPtr other);
LinkedListDefine(FilesystemTableEntry, FilesystemTableEntries, (EqualFunction) FilesystemTableEntry__equals);

typedef struct FilesystemTable
{
    FilesystemTableEntries entries;
}
FilesystemTable;

typedef FilesystemTable * FilesystemTablePtr;

FilesystemTable FilesystemTable__new();
Result(CollectionCode) FilesystemTable__add(
    FilesystemTablePtr self, KString device_path, KString filesystem_type, KString mountpoint
);

OptionalDef(FilesystemPtr);
ResultDef(FilesystemPtr);
MapDefine(KString, FilesystemPtr, MAX_MOUNTS, Mounts, kstring_equals);

typedef struct FilesystemNode FilesystemNode;
typedef FilesystemNode * FilesystemNodePtr;

typedef struct FilesystemIOState FilesystemIOState;
typedef FilesystemIOState * FilesystemIOStatePtr;

typedef struct Filesystem
{
    KString name;
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
    KString path;
    FilePointer offset;
    IOBuffer buffer;
}
FileHandleTarget;

typedef FileHandleTarget * FileHandleTargetPtr;

OptionalDef(FileHandleTarget);
OptionalDef(FileHandleTargetPtr);

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

FileHandleTargetPtr FileHandleTarget__heap_new(KString path);
void FileHandleTarget__dispose(FileHandleTargetPtr self);

FilesystemPtr Filesystem__heap_new(KString name, FileIO file_io);
FilesystemPtr RootFilesystem__heap_new();

Result(MountResult) Filesystem__mount(FilesystemPtr root, KString path, FilesystemPtr filesystem);

FilesystemIOState FilesystemIOState__new(FilesystemPtr filesystem);
FilesystemIOStatePtr FilesystemIOState__heap_new(FilesystemPtr filesystem);

#endif
