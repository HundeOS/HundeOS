#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_MEMFS_MEMFS_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_MEMFS_MEMFS_H

#include "../filesystem.h"
#include "../../../../types/linked_list.h"

typedef struct MemFilePage
{
    KernelAddress address;
}
MemFilePage;

typedef MemFilePage * MemFilePagePtr;

OptionalDef(MemFilePage);
OptionalDef(MemFilePagePtr);

bool MemFilePage__equals(MemFilePagePtr self, MemFilePagePtr other);
LinkedListDefine(MemFilePage, MemFilePages, (EqualFunction) MemFilePage__equals);

typedef struct MemFile
{
    BString name;
    size_t size;
    size_t capacity;
    MemFilePages pages;
}
MemFile;

typedef MemFile * MemFilePtr;

OptionalDef(MemFile);
OptionalDef(MemFilePtr);
ResultDef(MemFilePtr);

bool MemFile__equals(MemFilePtr self, MemFilePtr other);
LinkedListDefine(MemFile, MemFiles, (EqualFunction) MemFile__equals);

typedef struct MemDirectory
{
    BString path;
}
MemDirectory;

typedef MemDirectory * MemDirectoryPtr;

OptionalDef(MemDirectory);
OptionalDef(MemDirectoryPtr);

bool MemDirectory__equals(MemDirectoryPtr self, MemDirectoryPtr other);
LinkedListDefine(MemDirectory, MemDirectories, (EqualFunction) MemDirectory__equals);

typedef struct MemFileHandle
{
    BString path;
    FilePointer offset;
    IOBuffer buffer;
    Flags flags;
}
MemFileHandle;

typedef MemFileHandle * MemFileHandlePtr;

OptionalDef(MemFileHandle);
OptionalDef(MemFileHandlePtr);

MapDefine(IOInternalHandle, MemFileHandlePtr, MAX_HANDLES_PER_DEVICE, MemFileHandleMap, SimpleEquals(uint32_t));

typedef struct MemFilesystemIOState
{
    FilesystemIOState state;
    MemFiles files;
    MemDirectories directories;
    MemFileHandleMap handles;
    uint32_t handle_count;
}
MemFilesystemIOState;

typedef MemFilesystemIOState * MemFilesystemIOStatePtr;

FileIO MemFilesystemIO__new();
MemFilesystemIOStatePtr MemFilesystemIOState__heap_new(FilesystemPtr fs);

#endif
