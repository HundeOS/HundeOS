#ifndef HUND_IO_STREAM_REGISTRY_H
#define HUND_IO_STREAM_REGISTRY_H
#include "protocol.h"
#include "types/linked_list.h"

ResultDef(IOStatus);

typedef struct StreamDevice
{
    IORoute route;
    Flags access;
} StreamDevice;

typedef StreamDevice * StreamDevicePtr;
OptionalDef(StreamDevice);
bool StreamDevice__equals(void * first, void * second);
LinkedListDefine(StreamDevice, StreamDevices, StreamDevice__equals);

typedef struct StreamRegistry
{
    StreamDevices devices;
} StreamRegistry;

typedef StreamRegistry * StreamRegistryPtr;
StreamRegistry StreamRegistry__new(void);
Result(IOStatus)
    StreamRegistry__register(StreamRegistryPtr self, ConstBStringPtr name, Flags access, ConstBStringPtr service);
IOReply StreamRegistry__route(StreamRegistryPtr self, IORequestPtr request);
void StreamRegistry__dispose(StreamRegistryPtr self);
#endif
