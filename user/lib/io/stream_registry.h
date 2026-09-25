#ifndef HUND_IO_STREAM_REGISTRY_H
#define HUND_IO_STREAM_REGISTRY_H
#include "mediator.h"
#include "types/linked_list.h"

ResultDef(IOStatus);

typedef struct StreamDevice
{
    BString name;
    BString service;
    Flags access;
} StreamDevice;

typedef StreamDevice * StreamDevicePtr;
OptionalDef(StreamDevice);
bool StreamDevice__equals(void * first, void * second);
LinkedListDefine(StreamDevice, StreamDevices, StreamDevice__equals);

typedef struct StreamBackend
{
    BString service;
    IOMediator mediator;
    IOHandle first_handle;
} StreamBackend;

typedef StreamBackend * StreamBackendPtr;
OptionalDef(StreamBackend);
bool StreamBackend__equals(void * first, void * second);
LinkedListDefine(StreamBackend, StreamBackends, StreamBackend__equals);

typedef struct StreamRegistry
{
    StreamDevices devices;
    StreamBackends backends;
    IOHandle next_handle;
} StreamRegistry;

typedef StreamRegistry * StreamRegistryPtr;
StreamRegistry StreamRegistry__new(void);
Result(IOStatus)
    StreamRegistry__register(StreamRegistryPtr self, ConstBStringPtr name, Flags access, ConstBStringPtr service);
Optional(IOReply)
    StreamRegistry__execute(StreamRegistryPtr self, QueueConnectionID session, uint64_t pid, IORequestPtr request);
bool StreamRegistry__disconnect(StreamRegistryPtr self, QueueConnectionID session, uint64_t pid);
void StreamRegistry__dispose(StreamRegistryPtr self);
#endif
