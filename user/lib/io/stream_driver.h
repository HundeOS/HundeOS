#ifndef HUND_IO_STREAM_DRIVER_H
#define HUND_IO_STREAM_DRIVER_H
#include "io/server.h"

typedef struct IOStreamResource
{
    IOHandle handle;
    QueueConnectionID session;
    uint64_t pid;
    Flags flags;
} IOStreamResource;

typedef IOStreamResource * IOStreamResourcePtr;
OptionalDef(IOStreamResource);
OptionalDef(IOStreamResourcePtr);
bool IOStreamResource__equals(void * first, void * second);
FixedArrayDefine(IOStreamResource, 64, IOStreamResources, IOStreamResource__equals);

typedef IOReply (*IOStreamTransfer)(IOServiceContext context, IORequestPtr request);

typedef struct IOStreamDriver
{
    IOStreamResources resources;
    IOHandle next_handle;
    Flags access;
    IOServiceContext context;
    IOStreamTransfer transfer;
} IOStreamDriver;

typedef IOStreamDriver * IOStreamDriverPtr;
IOStreamDriver IOStreamDriver__new(Flags access, IOServiceContext context, IOStreamTransfer transfer);
Optional(IOReply)
    IOStreamDriver__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request);
bool IOStreamDriver__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid);
#endif
