#ifndef HUND_FILESYSTEM_NAMESPACE_H
#define HUND_FILESYSTEM_NAMESPACE_H
#include "io/server.h"
#include "io/mount_config.h"

typedef struct FilesystemMount
{
    MountConfig config;
    uint64_t device_path_length;
    uint8_t device_path_data[IO_PROTOCOL_DATA_SIZE];
    uint64_t filesystem_type_length;
    uint8_t filesystem_type_data[IO_PROTOCOL_DATA_SIZE];
    uint64_t mountpoint_length;
    uint8_t mountpoint_data[IO_PROTOCOL_DATA_SIZE];
} FilesystemMount;

typedef FilesystemMount * FilesystemMountPtr;
OptionalDef(FilesystemMountPtr);
OptionalDef(FilesystemMount);
bool FilesystemMount__equals(void * first, void * second);
bool FilesystemMount__matches_mountpoint(FilesystemMountPtr self, void * context);
FixedArrayDefine(FilesystemMount, 16, FilesystemMounts, FilesystemMount__equals);

typedef struct FilesystemNamespace
{
    FilesystemMounts mounts;
} FilesystemNamespace;

typedef FilesystemNamespace * FilesystemNamespacePtr;
ResultDef(IOStatus);
FilesystemNamespace FilesystemNamespace__new(void);
Result(IOStatus) FilesystemNamespace__add(FilesystemNamespacePtr self, MountConfig config);
Optional(IOReply) FilesystemNamespace__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid,
                                               IORequestPtr request);
bool FilesystemNamespace__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid);
#endif
