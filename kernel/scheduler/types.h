#ifndef __KERNEL_SCHEDULER_TYPES_H
#define __KERNEL_SCHEDULER_TYPES_H

#include "../types/optional.h"

typedef int64_t ReturnCode;
typedef uint64_t OperationKey;
typedef uint64_t ParkReason;

typedef enum ParkReasons
{
    PARK_WAIT_CHILD = 1,
    PARK_WAIT_IO = 2,
    PARK_WAIT_SERVICE_EVENT = 3,
    PARK_WAIT_CHILD_EVENT = 4,
    PARK_WAIT_SERVICE_READY = 5,
} ParkReasons;

typedef struct Scheduler Scheduler;
typedef Scheduler * SchedulerPtr;

OptionalDef(SchedulerPtr);
ResultDef(SchedulerPtr);

typedef struct ParkedOperation ParkedOperation;
typedef ParkedOperation * ParkedOperationPtr;

OptionalDef(ReturnCode);
OptionalDef(OperationKey);
OptionalDef(ParkReason);

typedef Result(Anything) (* Continuation)(ParkedOperationPtr parked_operation);

OptionalDef(Continuation);

#endif
