#ifndef __KERNEL_SERVICE_EVENT_DISPATCHER_H
#define __KERNEL_SERVICE_EVENT_DISPATCHER_H

#include <stdbool.h>
#include <stdint.h>

#include "../mutex.h"
#include "../process.h"
#include "../system_info.h"
#include "../types/map.h"
#include "../types/optional.h"
#include "../types/result.h"
#include "service.h"

typedef uint64_t IRQNumber;
OptionalDef(IRQNumber);

typedef struct IRQService
{
    ProcessPtr process;
    bool pending;
    bool enabled;
    uint64_t delivery_count;
    uint64_t coalesced_count;
} IRQService;

typedef IRQService * IRQServicePtr;

OptionalDef(IRQService);
OptionalDef(IRQServicePtr);

MapDefine(IRQNumber, IRQService, 256, IRQServiceMap, SimpleEquals(uint64_t));

typedef struct ServiceReadyWaiter
{
    ProcessPtr process;
    BString name;
} ServiceReadyWaiter;

typedef ServiceReadyWaiter * ServiceReadyWaiterPtr;

OptionalDef(ServiceReadyWaiter);
OptionalDef(ServiceReadyWaiterPtr);

typedef struct ServiceReadyName
{
    BString name;
    ProcessPtr process;
} ServiceReadyName;

typedef ServiceReadyName * ServiceReadyNamePtr;

OptionalDef(ServiceReadyName);
OptionalDef(ServiceReadyNamePtr);

bool ServiceReadyWaiter__equals(ServiceReadyWaiterPtr self, ServiceReadyWaiterPtr other);
bool ServiceReadyName__equals(ServiceReadyNamePtr self, ServiceReadyNamePtr other);
FixedArrayDefine(ServiceReadyName, MAX_SERVICE_READY_NAMES, ServiceReadyNames, (EqualFunction)ServiceReadyName__equals);
FixedArrayDefine(ServiceReadyWaiter, MAX_SERVICE_READY_WAITERS, ServiceReadyWaiters,
                 (EqualFunction)ServiceReadyWaiter__equals);
FixedArrayDefine(ProcessPtr, MAX_SERVICE_READY_WAITERS, ServiceReadyProcesses, &Ptr__equals);

typedef ServiceReadyProcesses * ServiceReadyProcessesPtr;

typedef struct ServiceEventDispatcher
{
    IRQServiceMap irq_services;
    ServiceReadyNames ready_names;
    ServiceReadyWaiters ready_waiters;
    mutex_t lock;
} ServiceEventDispatcher;

typedef ServiceEventDispatcher * ServiceEventDispatcherPtr;

typedef struct ServiceEventDispatch
{
    Optional(ProcessPtr) process;
} ServiceEventDispatch;

ResultDef(ServiceEventDispatch);
OptionalDef(ServiceEventDispatcherPtr);

ServiceEventDispatcher ServiceEventDispatcher__new();
Result(ServiceCode)
    ServiceEventDispatcher__subscribe_irq(ServiceEventDispatcherPtr self, ProcessPtr process, IRQNumber irq);
Result(ServiceEventDispatch)
    ServiceEventDispatcher__dispatch_irq(ServiceEventDispatcherPtr self,
                                         InterruptControllerInfoPtr interrupt_controller, IRQNumber irq);
Result(ServiceCode)
    ServiceEventDispatcher__enqueue(ServiceEventDispatcherPtr self, ProcessPtr process, ServiceEvent event);
Result(ServiceEvent) ServiceEventDispatcher__take_next(ServiceEventDispatcherPtr self, ProcessPtr process);
Result(ServiceCode)
    ServiceEventDispatcher__complete_active_irq(ServiceEventDispatcherPtr self,
                                                InterruptControllerInfoPtr interrupt_controller, ProcessPtr process);
Result(ServiceCode) ServiceEventDispatcher__unsubscribe_process(ServiceEventDispatcherPtr self, ProcessPtr process);
bool ServiceEventDispatcher__is_ready(ServiceEventDispatcherPtr self, ConstBStringPtr name);
Optional(ProcessPtr) ServiceEventDispatcher__ready_process(ServiceEventDispatcherPtr self, ConstBStringPtr name);
Result(ServiceCode)
    ServiceEventDispatcher__publish_ready(ServiceEventDispatcherPtr self, ProcessPtr process, ConstBStringPtr name,
                                          ServiceReadyProcessesPtr ready_processes);
Result(ServiceCode)
    ServiceEventDispatcher__wait_ready(ServiceEventDispatcherPtr self, ProcessPtr process, ConstBStringPtr name);
Result(ServiceCode) ServiceEventDispatcher__remove_ready_waiter(ServiceEventDispatcherPtr self, ProcessPtr process);

#define NEW_SERVICE_EVENT_DISPATCHER                                                                                   \
    (ServiceEventDispatcher)                                                                                           \
    {                                                                                                                  \
        .irq_services = EmptyMap(IRQServiceMap), .ready_names = EmptyArray(ServiceReadyNames),                         \
        .ready_waiters = EmptyArray(ServiceReadyWaiters), .lock = MUTEX_INIT,                                          \
    }

#endif
