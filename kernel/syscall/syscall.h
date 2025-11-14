#ifndef __KERNEL_SYSCALL_SYSCALL_H
#define __KERNEL_SYSCALL_SYSCALL_H

#include <stdint.h>
#include "../types/types.h"
#include "../types/result.h"
#include "../process.h"

typedef uint64_t SystemCallResult;
ResultDef(SystemCallResult);

typedef enum SystemCall
{
    PRINT = 0x21,
    PROC_EXIT = 0x00,
    IO_CALL = 0x80,
    PING = 0xFF,
} SystemCall;

typedef enum SystemCallResults
{
    SYSCALL_OK = 0,
    SYSCALL_FAILURE = -1,
    SYSCALL_IO_BLOCK = -2
} SystemCallResults;

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


#endif