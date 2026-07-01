#ifndef __KERNEL_SYSCALL_SYSLOG_HANDLER_H
#define __KERNEL_SYSCALL_SYSLOG_HANDLER_H

#include "../kernel_core.h"
#include "syscall.h"

Result(SystemCallResult) Syslog__handler(
    KernelPtr kernel,
    Optional(ProcessPtr) proc,
    Anything arg1,
    Anything arg2,
    Anything arg3,
    Anything arg4,
    Anything arg5
);

#endif
