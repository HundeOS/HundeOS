#ifndef __KERNEL_SYSCALL_PRINT_HANDLER_H
#define __KERNEL_SYSCALL_PRINT_HANDLER_H
#include "syscall_dispatcher.h"

Result(SystemCallResult) Print__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                        Anything arg3, Anything arg4, Anything arg5);

#endif
