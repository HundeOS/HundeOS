#ifndef HUND_KERNEL_SYSCALL_SYSTEM_INFO_HANDLER_H
#define HUND_KERNEL_SYSCALL_SYSTEM_INFO_HANDLER_H

#include "../kernel_core.h"
#include "syscall.h"

Result(SystemCallResult) SystemInfo__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                             Anything arg2, Anything arg3, Anything arg4, Anything arg5);

#endif
