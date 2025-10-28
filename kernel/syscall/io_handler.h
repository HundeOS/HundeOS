#ifndef __KERNEL_SYSCALL_IO_HANDLER_H
#define __KERNEL_SYSCALL_IO_HANDLER_H

#include <stdint.h>
#include "syscall_dispatcher.h"
#include "../types/types.h"
#include "../process.h"
#include "../kernel_init.h"

typedef enum IOOperation
{
    IO_OPEN,
    IO_CLOSE,
    IO_READ,
    IO_WRITE,
} IOOperation;

Result(SystemCallResult) IO__handler(
        KernelPtr kernel, ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5
    );

#endif