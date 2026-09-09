#ifndef __KERNEL_SYSCALL_SERVICE_HANDLER_H
#define __KERNEL_SYSCALL_SERVICE_HANDLER_H

#include "syscall_dispatcher.h"
#include "syscalls/service.h"

Result(SystemCallResult) Service__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) process, Anything service_operation,
                                          Anything arg2, Anything arg3, Anything arg4, Anything arg5);

#endif
