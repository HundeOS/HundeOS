#ifndef HUND_IO_STREAM_DRIVER_H
#define HUND_IO_STREAM_DRIVER_H
#include "io/server.h"

typedef struct IOStreamResource
{
    IOHandle handle;
    IOResourceOwners owners;
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
    IOInheritanceRegistry inheritance;
    IOHandle next_handle;
    Flags access;
    IOServiceContext context;
    IOStreamTransfer transfer;
    bool terminal;
    IOTerminalAttributes terminal_attributes;
} IOStreamDriver;

typedef IOStreamDriver * IOStreamDriverPtr;
IOStreamDriver IOStreamDriver__new(Flags access, IOServiceContext context, IOStreamTransfer transfer);
IOStreamDriver IOStreamDriver__new_terminal(Flags access, IOServiceContext context, IOStreamTransfer transfer);
Optional(IOReply)
    IOStreamDriver__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request);
bool IOStreamDriver__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid);
#endif
