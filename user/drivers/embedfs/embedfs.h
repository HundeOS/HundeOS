#ifndef HUND_USER_EMBEDFS_H
#define HUND_USER_EMBEDFS_H
#include "fs/embedded_fs.h"
#include "io/server.h"

typedef struct EmbedfsHandle
{
    IOHandle id;
    QueueConnectionID session;
    uint64_t pid;
    EmbeddedFsFile file;
    size_t offset;
} EmbedfsHandle;

typedef EmbedfsHandle * EmbedfsHandlePtr;
OptionalDef(EmbedfsHandle);
bool EmbedfsHandle__equals(void * first, void * second);
FixedArrayDefine(EmbedfsHandle, 32, EmbedfsHandles, EmbedfsHandle__equals);

typedef struct Embedfs
{
    EmbeddedFs archive;
    EmbedfsHandles handles;
    BStringArray children;
    IOHandle next_handle;
} Embedfs;

typedef Embedfs * EmbedfsPtr;
void Embedfs__init(EmbedfsPtr self, EmbeddedFsPtr archive);
IOReply Embedfs__execute(EmbedfsPtr self, IORequestPtr request);
Optional(IOReply)
    Embedfs__serve(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request);
bool Embedfs__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid);
#endif
