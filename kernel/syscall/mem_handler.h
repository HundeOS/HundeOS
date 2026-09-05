#ifndef __KERNEL_SYSCALL_MEM_HANDLER_H
#define __KERNEL_SYSCALL_MEM_HANDLER_H

#include "syscall_dispatcher.h"
#include "syscalls/mem.h"

Result(SystemCallResult) Mem__handler(
    KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything mem_operation,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
);

#endif
