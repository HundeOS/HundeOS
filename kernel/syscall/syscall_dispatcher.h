#ifndef __KERNEL_SYSCALL_HANDLER_H
#define __KERNEL_SYSCALL_HANDLER_H
#include <stdint.h>

#include "../process.h"
#include "../types/types.h"
#include "../types/result.h"
#include "../mutex.h"


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

typedef Result(SystemCallResult)(* Handler)(ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3);

OptionalDef(Handler);
ResultDef(Handler);
FixedArrayDefine(Handler, 256, Handlers);


typedef struct SyscallDispatcher
{
    Handlers handlers;
    mutex_t init_lock;
    bool init_done;
} SyscallDispatcher;

typedef SyscallDispatcher * SyscallDispatcherPtr;

Result(SystemCallResult) SyscallDispatcher__dispatch(SyscallDispatcherPtr self,
                                                  Optional(ProcessPtr) process,
                                                  SystemCall syscall_id,
                                                  Anything arg1,
                                                  Anything arg2,
                                                  Anything arg3);

Result(RegisterResult) SyscallDispatcher__register(SyscallDispatcherPtr self, SystemCall syscall, Handler handler);

#define NEW_SYSCALL_DISPATCHER \
    (SyscallDispatcher) { \
        .handlers = EmptyArray(Handlers), \
        .init_lock = MUTEX_INIT, \
        .init_done = false \
    } \

#endif