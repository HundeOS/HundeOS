#ifndef __KERNEL_SUBSYSTEMS_IO_SOCKET_SOCKET_H
#define __KERNEL_SUBSYSTEMS_IO_SOCKET_SOCKET_H

#include "../../../process.h"
#include "../../../scheduler/kernel_context.h"
#include "../../../subsystems/message/message_broker.h"
#include "../../../mutex.h"
#include "../../../types/map.h"
#include "../../../types/fixed_array.h"
#include "../io.h"
#include "../types.h"
#include "syscalls/socket.h"

typedef struct SocketRegistry SocketRegistry;
typedef SocketRegistry * SocketRegistryPtr;
OptionalDef(SocketRegistryPtr);

typedef struct SocketRequestState SocketRequestState;
typedef SocketRequestState * SocketRequestStatePtr;

OptionalDef(SocketRequestStatePtr);
OptionalDef(MessageBrokerPtr);
ResultDef(SocketRequestStatePtr);
ResultDef(SocketRegistryPtr);

MapDefine(uint64_t, SocketRequestStatePtr, IO_MAX_DEVICES, SocketRequestMap, SimpleEquals(uint64_t));

struct SocketRegistry
{
    mutex_t lock;
    Optional(ProcessPtr) router_process;
    Optional(MessageBrokerPtr) message_broker;
    SocketRequestMap requests;
    uint64_t next_request_id;
};

#define SOCKET_REGISTRY_INIT \
    (SocketRegistry) { \
        .lock = MUTEX_INIT, \
        .router_process = None(ProcessPtr), \
        .message_broker = None(MessageBrokerPtr), \
        .requests = EmptyMap(SocketRequestMap), \
        .next_request_id = 1, \
    }

ResultDef(IOSocketRouterRequest);

typedef Result(Anything) (* SocketRequestComplete)(
    IOSocketRouterRequestPtr request,
    IOSocketRouterCompletionPtr completion,
    Anything context
);

Result(ServiceCode) SocketRegistry__register_router(
    SocketRegistryPtr self,
    ProcessPtr service_process,
    MessageBrokerPtr message_broker
);
Result(ServiceCode) SocketRegistry__start_request(
    SocketRegistryPtr self,
    IOSocketRouterRequest request,
    KernelContext kernel_context,
    SocketRequestComplete complete,
    Anything complete_context
);
Result(IOSocketRouterRequest) SocketRegistry__get_request(
    SocketRegistryPtr self,
    ProcessPtr service_process,
    uint64_t request_id
);
Result(ServiceCode) SocketRegistry__complete(
    SocketRegistryPtr self,
    ProcessPtr service_process,
    IOSocketRouterCompletionPtr completion
);
void SocketRegistry__reset(SocketRegistryPtr self);

typedef struct SocketIOState SocketIOState;
typedef SocketIOState * SocketIOStatePtr;

typedef struct SocketIO SocketIO;
typedef SocketIO * SocketIOPtr;

struct SocketIO
{
    IO base_io;
    SocketRegistry registry;
};

SocketIOStatePtr SocketIOState__heap_new(MessageBrokerPtr message_broker, SocketIOPtr socket_io);
SocketIOPtr SocketIO__heap_new(void);
Result(SocketRegistryPtr) SocketIO__registered_registry(IOSubsystemPtr io_subsystem);

#endif
