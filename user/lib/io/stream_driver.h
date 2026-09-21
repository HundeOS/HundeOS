#ifndef HUND_IO_STREAM_DRIVER_H
#define HUND_IO_STREAM_DRIVER_H
#include "io/mediator.h"

typedef IOReply (*IOStreamTransfer)(IOServiceContext context, IORequestPtr request);
typedef struct IOStreamDriver
{
    IOResources resources;
    IOHandle next_handle;
    QueueConnectionID mediator;
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
