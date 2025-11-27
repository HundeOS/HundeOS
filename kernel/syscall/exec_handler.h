#ifndef __KERNEL_SYSCALL_EXEC_H
#define __KERNEL_SYSCALL_EXEC_H

#include "syscall_dispatcher.h"

Result(SystemCallResult) Exec__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything arg1,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
);


#endif