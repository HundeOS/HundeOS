#ifndef __KERNEL_SYSCALL_PROCESS_HANDLER_H
#define __KERNEL_SYSCALL_PROCESS_HANDLER_H

#include "../kernel_core.h"
#include "syscall.h"

Result(SystemCallResult) ProcessList__handler(
    KernelPtr kernel,
    Optional(ProcessPtr) proc,
    Anything arg1,
    Anything arg2,
    Anything arg3,
    Anything arg4,
    Anything arg5
);
Result(SystemCallResult) ProcessWaitChildEvent__handler(
    KernelPtr kernel,
    Optional(ProcessPtr) proc,
    Anything arg1,
    Anything arg2,
    Anything arg3,
    Anything arg4,
    Anything arg5
);
Result(SystemCallResult) ProcessSleep__handler(
    KernelPtr kernel,
    Optional(ProcessPtr) proc,
    Anything arg1,
    Anything arg2,
    Anything arg3,
    Anything arg4,
    Anything arg5
);

#endif
