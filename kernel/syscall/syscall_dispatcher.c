#include "syscall_dispatcher.h"
#include <stdint.h>

#include "../debug.h"
#include "../print.h"
#include "../types/fixed_array.h"
#include "../types/result.h"
#include "../types/optional.h"
#include "../mutex.h"
#include "../process.h"
#include "../cpu.h"

FixedArrayBodyDefine(Handlers);

static SyscallDispatcher syscall_handler = (SyscallDispatcher) {
    .handlers = EmptyArray(Handlers),
};

const char * UNHANDLED_SYSCALL = "Unhandled syscall";
const char * FAILED_TO_DETERMINE_PROCESS = "Failed to determine current process";


SyscallDispatcherPtr SyscallHandler__instance()
{
    return &syscall_handler;
}

Result(RegisterResult) SyscallDispatcher__register(SyscallDispatcherPtr self, SystemCall syscall, Handler handler)
{
    ResultMatch(ResCode, Handlers__set(&self->handlers, syscall, handler));
        IsError(code, message,
            {
                return Error(RegisterResult, message, REGISTER_FAILURE);
            });
        IsSuccess(_,
            {
                return Success(RegisterResult, REGISTER_OK);
            });
    End
}

Result(SystemCallResult) SyscallDispatcher__dispatch(SyscallDispatcherPtr self,
                                                  Optional(ProcessPtr) process_opt,
                                                  SystemCall syscall_id,
                                                  Anything arg1,
                                                  Anything arg2,
                                                  Anything arg3)
{
    __d_printf(DEBUG, "SyscallHandler__handle(syscall_id=%i, arg1=%i, arg2=%i, arg3=%i)", syscall_id, arg1, arg2, arg3);

    ProcessPtr proc = NULL;
    Match(ProcessPtr, process_opt);
        IsSome(process,
        {
            proc = process;
        });
        IsNone(
        {
            return Error(SystemCallResult, KString__new(FAILED_TO_DETERMINE_PROCESS), SYSCALL_FAILURE);
        });
    End

    Match(Handler, Handlers__at(&self->handlers, syscall_id));
        IsSome(handler,
            {
                return handler(proc, arg1, arg2, arg3);
            });
        IsNone(
            {
                return Error(SystemCallResult, KString__new(UNHANDLED_SYSCALL), SYSCALL_FAILURE);
            })
    End;

    char buf[1024];
    k_sprintf(buf, "Unhandled syscall: %i", syscall_id);
    return Error(SystemCallResult, KString__new(buf), SYSCALL_FAILURE);
}