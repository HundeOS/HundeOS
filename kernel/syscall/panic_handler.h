#ifndef __KERNEL_SYSCALL_PANIC_HANDLER_H
#define __KERNEL_SYSCALL_PANIC_HANDLER_H

#include "syscall.h"

Result(SystemCallResult) Panic__handler(
    KernelPtr kernel,
    Optional(ProcessPtr) proc,
    Anything arg1,
    Anything arg2,
    Anything arg3,
    Anything arg4,
    Anything arg5
);

#endif
