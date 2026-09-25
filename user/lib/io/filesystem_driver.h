#ifndef HUND_FILESYSTEM_DRIVER_H
#define HUND_FILESYSTEM_DRIVER_H
#include "server.h"

#define FILESYSTEM_FILE_CAPACITY (12U * 1024U)
typedef uint64_t FilesystemFileSize;
ResultDef(FilesystemFileSize);
typedef struct FilesystemFileInfo
{
    FilesystemFileSize size;
    FileMode mode;
} FilesystemFileInfo;
static inline FilesystemFileInfo FilesystemFileInfo__new(FilesystemFileSize size, FileMode mode)
{
    return (FilesystemFileInfo){
        .size = size,
        .mode = mode,
    };
}
ResultDef(FilesystemFileInfo);
typedef uint8_t * FilesystemFileDataPtr;
OptionalDef(FilesystemFileDataPtr);
typedef Result(FilesystemFileSize) (*FilesystemLoad)(IOServiceContext context, ConstBStringPtr path,
                                                     FilesystemFileDataPtr data);
typedef Result(FilesystemFileInfo) (*FilesystemDescribe)(IOServiceContext context, ConstBStringPtr path);
typedef Result(FilesystemFileSize) (*FilesystemRead)(IOServiceContext context, ConstBStringPtr path,
                                                     FilesystemFileSize offset, FilesystemFileDataPtr data,
                                                     FilesystemFileSize capacity);
typedef IOStatus (*FilesystemStore)(IOServiceContext context, ConstBStringPtr path, FilesystemFileDataPtr data,
                                    FilesystemFileSize size);
typedef Result(DirectoryEntryCount) (*FilesystemList)(IOServiceContext context, ConstBStringPtr path,
                                                      DirectoryEntryPtr entries, size_t capacity, uint64_t offset);
typedef IOStatus (*FilesystemMkdir)(IOServiceContext context, ConstBStringPtr path);
typedef IOStatus (*FilesystemRemove)(IOServiceContext context, ConstBStringPtr path, Flags flags);
typedef IOStatus (*FilesystemLink)(IOServiceContext context, ConstBStringPtr source_path, ConstBStringPtr target_path);
typedef enum FilesystemSourceMode
{
    FILESYSTEM_SOURCE_SNAPSHOT,
    FILESYSTEM_SOURCE_STREAM,
} FilesystemSourceMode;
typedef struct FilesystemSnapshotOperations
{
    FilesystemLoad load;
} FilesystemSnapshotOperations;
typedef struct FilesystemStreamOperations
{
    FilesystemDescribe describe;
    FilesystemRead read;
} FilesystemStreamOperations;
typedef union FilesystemSourceOperations
{
    FilesystemSnapshotOperations snapshot;
    FilesystemStreamOperations stream;
} FilesystemSourceOperations;
typedef struct FilesystemOperations
{
    FilesystemSourceMode source_mode;
    FilesystemSourceOperations source;
    FilesystemStore store;
    FilesystemList list;
    FilesystemMkdir mkdir;
    FilesystemRemove remove;
    FilesystemLink link;
    FilesystemFileSize write_capacity;
} FilesystemOperations;
typedef const FilesystemOperations * ConstFilesystemOperationsPtr;
FilesystemOperations FilesystemOperations__new_snapshot(FilesystemLoad load, FilesystemStore store, FilesystemList list,
                                                        FilesystemMkdir mkdir, FilesystemRemove remove,
                                                        FilesystemLink link, FilesystemFileSize write_capacity);
FilesystemOperations FilesystemOperations__new_stream(FilesystemDescribe describe, FilesystemRead read,
                                                      FilesystemStore store, FilesystemList list, FilesystemMkdir mkdir,
                                                      FilesystemRemove remove, FilesystemLink link,
                                                      FilesystemFileSize write_capacity);
IOStatus FilesystemOperations__unsupported_link(IOServiceContext context, ConstBStringPtr source_path,
                                                ConstBStringPtr target_path);

typedef struct FilesystemFile
{
    IOHandle handle;
    QueueConnectionID session;
    uint64_t pid;
    BString path;
    Flags flags;
    FilesystemFileSize size;
    FilesystemFileSize offset;
    Optional(FilesystemFileDataPtr) data;
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
