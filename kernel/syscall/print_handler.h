#ifndef __KERNEL_SYSCALL_PRINT_HANDLER_H
#define __KERNEL_SYSCALL_PRINT_HANDLER_H
#include "syscall_handler.h"

Result(SystemCallResult) Print__handler(ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3);

#endif

