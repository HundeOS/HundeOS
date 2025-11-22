#ifndef __KERNEL_SYSCALL_HANDLER_H
#define __KERNEL_SYSCALL_HANDLER_H
#include <stdint.h>

#include "../process.h"
#include "../types/types.h"
#include "../types/result.h"
#include "../mutex.h"
#include "../kernel_init.h"
#include "syscall.h"

typedef enum RegisterResult
{
    REGISTER_OK = 0,
    REGISTER_FAILURE = -1,
} RegisterResult;

ResultDef(RegisterResult);

typedef Result(SystemCallResult)(* Handler)(
        KernelPtr kernel, Optional(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5
    );

OptionalDef(Handler);
ResultDef(Handler);
FixedArrayDefine(Handler, 256, Handlers, &Ptr__equals);

typedef struct SyscallDispatcher
{
    Handlers handlers;
    mutex_t init_lock;
    bool init_done;
}
SyscallDispatcher;

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