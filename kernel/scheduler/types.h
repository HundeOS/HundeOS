#ifndef __KERNEL_SCHEDULER_TYPES_H
#define __KERNEL_SCHEDULER_TYPES_H

#include "../types/optional.h"

typedef int64_t ReturnCode;
typedef uint64_t OperationKey;

typedef struct Scheduler Scheduler;
typedef Scheduler * SchedulerPtr;

OptionalDef(SchedulerPtr);
ResultDef(SchedulerPtr);

typedef struct ParkedOperation ParkedOperation;
typedef ParkedOperation * ParkedOperationPtr;

OptionalDef(ReturnCode);
OptionalDef(OperationKey);

typedef Result(Anything) (* Continuation)(ParkedOperationPtr parked_operation);

OptionalDef(Continuation);

#endif
