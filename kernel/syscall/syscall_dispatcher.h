#ifndef __KERNEL_SYSCALL_HANDLER_H
#define __KERNEL_SYSCALL_HANDLER_H
#include <stdint.h>
#include <stdint.h>

#include "../process.h"
#include "../types/types.h"
#include "../types/result.h"
#include "../mutex.h"
#include "../kernel_init.h"


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

typedef enum RegisterResult
{
    REGISTER_OK = 0,
    REGISTER_FAILURE = -1,
} RegisterResult;

typedef uint64_t SystemCallResult;

ResultDef(SystemCallResult);
ResultDef(RegisterResult);

typedef Result(SystemCallResult)(* Handler)(
        KernelPtr kernel, ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5
    );

OptionalDef(Handler);
ResultDef(Handler);
FixedArrayDefine(Handler, 256, Handlers, &Ptr__equals);


typedef struct SyscallDispatcher
{
    Handlers handlers;
    mutex_t init_lock;
    bool init_done;
} SyscallDispatcher;

typedef SyscallDispatcher * SyscallDispatcherPtr;

Result(SystemCallResult) SyscallDispatcher__dispatch(SyscallDispatcherPtr self,
                                                     KernelPtr kernel,
                                                     Optional(ProcessPtr) process_opt,
                                                     SystemCall syscall_id,
                                                     Anything arg1,
                                                     Anything arg2,
                                                     Anything arg3,
                                                     Anything arg4,
                                                     Anything arg5);

Result(RegisterResult) SyscallDispatcher__register(SyscallDispatcherPtr self, SystemCall syscall, Handler handler);

#define NEW_SYSCALL_DISPATCHER \
    (SyscallDispatcher) { \
        .handlers = EmptyArray(Handlers), \
        .init_lock = MUTEX_INIT, \
        .init_done = false \
    } \

#endif