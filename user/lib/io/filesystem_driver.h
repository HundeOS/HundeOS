#ifndef HUND_FILESYSTEM_DRIVER_H
#define HUND_FILESYSTEM_DRIVER_H
#include "server.h"

#define FILESYSTEM_FILE_CAPACITY (12U * 1024U)
typedef uint64_t FilesystemFileSize;
ResultDef(FilesystemFileSize);
OptionalDef(DirectoryEntry);
typedef Optional(DirectoryEntry) OptionalDirectoryEntry;
ResultDef(OptionalDirectoryEntry);
typedef uint8_t * FilesystemFileDataPtr;
typedef Result(FilesystemFileSize) (*FilesystemLoad)(IOServiceContext context, ConstBStringPtr path,
                                                     FilesystemFileDataPtr data);
typedef IOStatus (*FilesystemStore)(IOServiceContext context, ConstBStringPtr path, FilesystemFileDataPtr data,
                                    FilesystemFileSize size);
typedef Result(OptionalDirectoryEntry) (*FilesystemList)(IOServiceContext context, ConstBStringPtr path,
                                                         uint64_t index);
typedef IOStatus (*FilesystemMkdir)(IOServiceContext context, ConstBStringPtr path);
typedef IOStatus (*FilesystemRemove)(IOServiceContext context, ConstBStringPtr path, Flags flags);
typedef struct FilesystemOperations
{
    FilesystemLoad load;
    FilesystemStore store;
    FilesystemList list;
    FilesystemMkdir mkdir;
    FilesystemRemove remove;
    FilesystemFileSize write_capacity;
} FilesystemOperations;
typedef const FilesystemOperations * ConstFilesystemOperationsPtr;
FilesystemOperations FilesystemOperations__new(FilesystemLoad load, FilesystemStore store, FilesystemList list,
                                               FilesystemMkdir mkdir, FilesystemRemove remove,
                                               FilesystemFileSize write_capacity);

typedef struct FilesystemFile
{
    IOHandle handle;
    QueueConnectionID session;
    uint64_t pid;
    BString path;
    Flags flags;
    FilesystemFileSize size;
    FilesystemFileSize offset;
    FilesystemFileDataPtr data;
} FilesystemFile;
typedef FilesystemFile * FilesystemFilePtr;
OptionalDef(FilesystemFile);
OptionalDef(FilesystemFilePtr);
bool FilesystemFile__equals(void * first, void * second);
FixedArrayDefine(FilesystemFile, 32, FilesystemFiles, FilesystemFile__equals);

typedef struct FilesystemDriver
{
    IOServiceContext volume;
    FilesystemOperations operations;
    FilesystemFiles files;
    IOHandle next_handle;
    QueueConnectionID namespace_session;
} FilesystemDriver;
typedef FilesystemDriver * FilesystemDriverPtr;
FilesystemDriver FilesystemDriver__new(IOServiceContext volume, ConstFilesystemOperationsPtr operations);
IOReply FilesystemDriver__execute(FilesystemDriverPtr self, IORequestPtr request);
bool FilesystemDriver__release(FilesystemDriverPtr self, QueueConnectionID session, uint64_t pid);
int FilesystemDriver__run(FilesystemDriverPtr self, ConstBStringPtr mountpoint);
#endif
