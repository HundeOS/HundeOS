#ifndef __KERNEL_SCHEDULER_REGISTER_H
#define __KERNEL_SCHEDULER_REGISTER_H

#include <stdint.h>
#include "../kernel_core.h"
#include "../types/result.h"

typedef uint64_t SchedulerRegisterCode;
ResultDef(SchedulerRegisterCode);

Result(SchedulerRegisterCode) Scheduler__register(KernelPtr kernel);


#endif