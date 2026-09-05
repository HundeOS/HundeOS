#ifndef __KERNEL_SYSCALL_SHUTDOWN_HANDLER_H
#define __KERNEL_SYSCALL_SHUTDOWN_HANDLER_H

#include "../kernel_core.h"
#include "syscall.h"

Result(SystemCallResult) Shutdown__handler(
    KernelPtr kernel,
    OptionalPtr(ProcessPtr) proc,
    Anything arg1,
    Anything arg2,
    Anything arg3,
    Anything arg4,
    Anything arg5
);

#endif
