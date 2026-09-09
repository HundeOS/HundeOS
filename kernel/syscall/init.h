#ifndef __KERNEL_SYSCALL_INIT_H
#define __KERNEL_SYSCALL_INIT_H

#include "syscall_dispatcher.h"

Result(RegisterResult) Init__syscalls(SyscallDispatcherPtr self);

#endif