#ifndef __KERNEL_SERVICE_SERVICE_H
#define __KERNEL_SERVICE_SERVICE_H

#include <stdint.h>

#include "../all_defs.h"
#include "../types/fixed_array.h"
#include "../types/result.h"
#include "../types/optional.h"
#include "../types/comparison.h"
#include "../types/types.h"
#include "../process_class.h"
#include "syscalls/service.h"

typedef struct Process Process;
typedef Process * ProcessPtr;
typedef struct Scheduler Scheduler;
typedef Scheduler * SchedulerPtr;
typedef struct KernelContext KernelContext;
typedef KernelContext * KernelContextPtr;
typedef struct ParkedOperation ParkedOperation;
typedef ParkedOperation * ParkedOperationPtr;

OptionalDef(ServiceEvent);
ResultDef(ServiceEvent);

ResultDef(ServiceCode);

bool ServiceEvent__equals(ServiceEventPtr self, ServiceEventPtr other);
FixedArrayDefine(ServiceEvent, MAX_SERVICE_EVENTS, ServiceEvents, (EqualFunction)ServiceEvent__equals);

typedef struct ServiceState
{
    ServiceEvents events;
    Optional(ServiceEvent) active_irq_event;
} ServiceState;

ServiceState ServiceState__new();
Result(ServiceEvent) Service__pop_next_event(ProcessPtr process);
Result(ServiceCode) Service__push_next_event(ProcessPtr process, ServiceEvent event);
Result(ServiceEvent) Service__complete_wait(ParkedOperationPtr parked_operation);

#endif
