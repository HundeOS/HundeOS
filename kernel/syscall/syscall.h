#ifndef __KERNEL_SYSCALL_SYSCALL_H
#define __KERNEL_SYSCALL_SYSCALL_H

#include <stdint.h>
#include "../types/types.h"
#include "../types/result.h"
#include "../process.h"

#include "syscalls/calls.h"

ResultDef(SystemCallResult);

typedef struct CallContext
{
    Optional(ProcessPtr) process_opt;
    SystemCall syscall_id;
    Anything arg1;
    Anything arg2;
    Anything arg3;
    Anything arg4;
    Anything arg5;
}
CallContext;

CallContext CallContext__new(
    Optional(ProcessPtr) process, SystemCall syscall_id, Anything arg1, Anything arg2,
    Anything arg3, Anything arg4, Anything arg5
);


#endif