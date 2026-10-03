#include "syscall_dispatcher.h"
#include <stdint.h>

#include "../types/types.h"

#include "../debug.h"
#include "../print.h"
#include "../types/fixed_array.h"
#include "../types/result.h"
#include "../types/optional.h"
#include "../mutex.h"
#include "../process.h"
#include "../kernel_core.h"
#include "../scheduler/scheduler.h"

MapBodyDefine(Handlers);

static BString unhandled_syscall = bstr("Unhandled syscall");
static BString unhandled_syscall_format = bstr("Unhandled syscall %i");

Result(RegisterResult) SyscallDispatcher__register(SyscallDispatcherPtr self, SystemCall syscall, Handler handler)
{
    match_result(
        Handlers__set(&self->handlers, syscall, handler),
        success(_)
        {
            return Success(RegisterResult, REGISTER_OK);
        },
        error(code, message)
        {
            return Error(RegisterResult, message, REGISTER_FAILURE);
        }
    );
}

Result(SystemCallResult)
    SyscallDispatcher__dispatch(SyscallDispatcherPtr self, KernelPtr kernel, OptionalPtr(ProcessPtr) process_opt,
                                SystemCall syscall_id, Anything arg1, Anything arg2, Anything arg3, Anything arg4,
                                Anything arg5)
{
    __d_printf(DEBUG, "SyscallHandler__handle(syscall_id=%i, arg1=%i, arg2=%i, arg3=%i, arg4=%i, arg5=%i)", syscall_id,
               arg1, arg2, arg3, arg4, arg5);

    match_optional(
        Handlers__get(&self->handlers, syscall_id),
        some(handler)
        {
            return handler(kernel, process_opt, arg1, arg2, arg3, arg4, arg5);
        },
        none()
        {
            return Error(SystemCallResult, unhandled_syscall, SYSCALL_FAILURE);
        }
    );

    return Error(SystemCallResult, BString__sprintf(&unhandled_syscall_format, syscall_id), SYSCALL_FAILURE);
}
