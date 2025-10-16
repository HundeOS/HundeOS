#include "syscall_handler.h"
#include <stdint.h>

#include "init.h"
#include "../debug.h"
#include "../print.h"
#include "../types/fixed_array.h"
#include "../types/result.h"
#include "../types/optional.h"
#include "../mutex.h"
#include "../process.h"
#include "../cpu.h"

FixedArrayDefine(SyscallHandler, 256, Handlers);
FixedArrayBodyDefine(Handlers);

static Handlers handlers = EmptyArray(Handlers);

const char * UNHANDLED_SYSCALL = "Unhandled syscall";
const char * FAILED_TO_DETERMINE_PROCESS = "Failed to determine current process";

Result(RegisterResult) SyscallHandler__register(SystemCall syscall, SyscallHandler handler)
{
    ResultMatch(ResCode, Handlers__set(&handlers, syscall, handler));
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

Result(SystemCallResult) SyscallHandler__dispatch(
    SystemCall syscall_id, Anything arg1, Anything arg2, Anything arg3
    )
{
    __d_printf(DEBUG, "SyscallHandler__handle(syscall_id=%i, arg1=%i, arg2=%i, arg3=%i)", syscall_id, arg1, arg2, arg3);

    ResultMatch(RegisterResult, Init__syscalls());
        IsError(code, message,
        {
            return Error(SystemCallResult, message, code);
        });
    End

    ProcessPtr proc = NULL;
    Match(ProcessPtr, Process__current_for_cpu(CPU__id()));
        IsSome(process,
        {
            proc = process;
        });
        IsNone(
        {
            return Error(SystemCallResult, KString__new(FAILED_TO_DETERMINE_PROCESS), SYSCALL_FAILURE);
        });
    End

    Match(SyscallHandler, Handlers__at(&handlers, syscall_id));
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