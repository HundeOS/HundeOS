#ifndef __KERNEL_SYSCALL_PING_HANDLER_H
#define __KERNEL_SYSCALL_PING_HANDLER_H

#include "syscall_dispatcher.h"

Result(SystemCallResult) Ping__handler(
        KernelPtr kernel, ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5
    );

#endif