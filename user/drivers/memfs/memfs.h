#ifndef HUND_USER_MEMFS_H
#define HUND_USER_MEMFS_H
#include "io/server.h"
#include "types/linked_list.h"

typedef uint64_t MemfsNodeID;
typedef uint8_t * MemfsDataPtr;
OptionalDef(MemfsDataPtr);
typedef struct MemfsNode
{
    MemfsNodeID id;
    BString path;
    bool directory;
    ProcessCredentials owner;
    FileMode mode;
    Optional(MemfsDataPtr) data;
    size_t size;
    size_t capacity;
} MemfsNode;
typedef MemfsNode * MemfsNodePtr;
OptionalDef(MemfsNode);
OptionalDef(MemfsNodePtr);
bool MemfsNode__equals(void * first, void * second);
LinkedListDefine(MemfsNode, MemfsNodes, MemfsNode__equals);

typedef struct MemfsHandle
{
    IOHandle id;
    MemfsNodeID node;
    QueueConnectionID session;
    uint64_t pid;
    Flags flags;
    size_t offset;
} MemfsHandle;
typedef MemfsHandle * MemfsHandlePtr;
OptionalDef(MemfsHandle);
bool MemfsHandle__equals(void * first, void * second);
FixedArrayDefine(MemfsHandle, 32, MemfsHandles, MemfsHandle__equals);

typedef struct Memfs
{
    MemfsNodes nodes;
    MemfsHandles handles;
    MemfsNodeID next_node;
    IOHandle next_handle;
    QueueConnectionID namespace_session;
} Memfs;
typedef Memfs * MemfsPtr;
Memfs Memfs__new(void);
void Memfs__dispose(MemfsPtr self);
IOReply Memfs__execute(MemfsPtr self, IORequestPtr request);
Optional(IOReply) Memfs__serve(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request);
bool Memfs__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid);
#endif
