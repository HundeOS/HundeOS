#ifndef HUND_FILESYSTEM_NAMESPACE_H
#define HUND_FILESYSTEM_NAMESPACE_H
#include "io/mediator.h"
#include "io/mount_config.h"

typedef struct FilesystemMount
{
    MountConfig config;
    IOMediator mediator;
    IOHandle first_handle;
    bool connected;
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
    QueueConnectionID mediator_session;
} FilesystemNamespace;
typedef FilesystemNamespace * FilesystemNamespacePtr;
ResultDef(IOStatus);
FilesystemNamespace FilesystemNamespace__new(void);
Result(IOStatus) FilesystemNamespace__add(FilesystemNamespacePtr self, MountConfig config);
Optional(IOReply) FilesystemNamespace__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid,
                                               IORequestPtr request);
bool FilesystemNamespace__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid);
#endif
