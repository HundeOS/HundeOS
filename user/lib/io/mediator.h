#ifndef HUND_IO_MEDIATOR_H
#define HUND_IO_MEDIATOR_H
#include "io/server.h"

typedef struct IOResource
{
    IOHandle handle;
    IOHandle backend;
    QueueConnectionID session;
    uint64_t pid;
    Flags flags;
    bool listener;
} IOResource;

typedef IOResource * IOResourcePtr;
OptionalDef(IOResource);
bool IOResource__equals(void * first, void * second);
FixedArrayDefine(IOResource, 64, IOResources, IOResource__equals);
bool IOPendingSession__equals(void * first, void * second);
FixedArrayDefine(QueueConnectionID, QUEUE_LIMIT, IOPendingSessions, IOPendingSession__equals);

typedef struct IOMediator
{
    Channel backend;
    BufferPair buffers;
    IOResources resources;
    IOPendingSessions pending_opens;
    IOHandle next_handle;
    IOHandle handle_limit;
    QueueConnectionID active;
    QueueConnectionID cleaning;
    IORequestOperation active_operation;
    bool failed;
    bool filesystem;
    bool stream;
} IOMediator;

typedef IOMediator * IOMediatorPtr;
IOMediator IOMediator__new(Channel backend);
IOMediator IOMediator__new_filesystem(Channel backend, IOHandle first_handle);
IOMediator IOMediator__new_stream(Channel backend, IOHandle first_handle);
Optional(IOReply)
    IOMediator__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request);
bool IOMediator__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid);
#endif
