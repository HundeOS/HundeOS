#ifndef __KERNEL_SCHEDULER_TYPES_H
#define __KERNEL_SCHEDULER_TYPES_H

#include "../types/optional.h"

typedef int64_t ReturnCode;

typedef struct Scheduler Scheduler;
typedef Scheduler * SchedulerPtr;

OptionalDef(SchedulerPtr);
ResultDef(SchedulerPtr);

typedef struct ParkedKernelContext ParkedKernelContext;
typedef ParkedKernelContext * ParkedKernelContextPtr;

OptionalDef(ReturnCode);

typedef Result(Anything) (* Continuation)(ParkedKernelContextPtr parked_context);

OptionalDef(Continuation);

#endif