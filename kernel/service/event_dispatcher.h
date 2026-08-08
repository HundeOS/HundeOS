#ifndef __KERNEL_SERVICE_EVENT_DISPATCHER_H
#define __KERNEL_SERVICE_EVENT_DISPATCHER_H

#include <stdbool.h>
#include <stdint.h>

#include "../mutex.h"
#include "../process.h"
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
}
IRQService;

typedef IRQService * IRQServicePtr;

OptionalDef(IRQService);
OptionalDef(IRQServicePtr);

MapDefine(IRQNumber, IRQService, 256, IRQServiceMap, SimpleEquals(uint64_t));

typedef struct ServiceEventDispatcher
{
    IRQServiceMap irq_services;
    mutex_t lock;
}
ServiceEventDispatcher;

typedef ServiceEventDispatcher * ServiceEventDispatcherPtr;

typedef struct ServiceEventDispatch
{
    Optional(ProcessPtr) process;
}
ServiceEventDispatch;

ResultDef(ServiceEventDispatch);
OptionalDef(ServiceEventDispatcherPtr);

ServiceEventDispatcher ServiceEventDispatcher__new();
Result(ServiceCode) ServiceEventDispatcher__subscribe_irq(
    ServiceEventDispatcherPtr self, ProcessPtr process, IRQNumber irq
);
Result(ServiceEventDispatch) ServiceEventDispatcher__dispatch_irq(ServiceEventDispatcherPtr self, IRQNumber irq);
Result(ServiceCode) ServiceEventDispatcher__enqueue(
    ServiceEventDispatcherPtr self, ProcessPtr process, ServiceEvent event
);
Result(ServiceEvent) ServiceEventDispatcher__take_next(ServiceEventDispatcherPtr self, ProcessPtr process);
Result(ServiceCode) ServiceEventDispatcher__complete_active_irq(
    ServiceEventDispatcherPtr self, ProcessPtr process
);
Result(ServiceCode) ServiceEventDispatcher__unsubscribe_process(
    ServiceEventDispatcherPtr self, ProcessPtr process
);

#define NEW_SERVICE_EVENT_DISPATCHER \
    (ServiceEventDispatcher) { \
        .irq_services = EmptyMap(IRQServiceMap), \
        .lock = MUTEX_INIT, \
    }

#endif
