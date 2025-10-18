#ifndef __KERNEL_SYSCALL_EXIT_HANDLER_H
#define __KERNEL_SYSCALL_EXIT_HANDLER_H

#include "syscall_dispatcher.h"

Result(SystemCallResult) Exit__handler(ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3);

#endif