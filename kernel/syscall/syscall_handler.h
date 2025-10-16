#ifndef __KERNEL_SYSCALL_HANDLER_H
#define __KERNEL_SYSCALL_HANDLER_H
#include <stdint.h>

#include "../process.h"
#include "../types/types.h"
#include "../types/result.h"


typedef enum SystemCall
{
    PRINT = 0x21,
    IO_READ_SYNC = 0x22,
    IO_WRITE_SYNC = 0x23,
    PROC_EXIT = 0x00,
    PING = 0xFF,
} SystemCall;

typedef enum SystemCallResult
{
    SYSCALL_OK = 0,
    SYSCALL_FAILURE = -1,
    SYSCALL_IO_BLOCK = -2
} SystemCallResult;

typedef enum RegisterResult
{
    REGISTER_OK = 0,
    REGISTER_FAILURE = -1,
} RegisterResult;

ResultDef(SystemCallResult);
ResultDef(RegisterResult);

typedef Result(SystemCallResult)(* SyscallHandler)(ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3);
OptionalDef(SyscallHandler);
ResultDef(SyscallHandler);

Result(SystemCallResult) SyscallHandler__dispatch(
        SystemCall syscall_id, Anything arg1, Anything arg2, Anything arg3
    );

Result(RegisterResult) SyscallHandler__register(SystemCall syscall, SyscallHandler handler);

#endif