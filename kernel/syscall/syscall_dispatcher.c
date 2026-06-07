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

FixedArrayBodyDefine(Handlers);

static KString unhandled_syscall = KSTRING_NEW("Unhandled syscall");
static KString unhandled_syscall_format = KSTRING_NEW("Unhandled syscall %i");


Result(RegisterResult) SyscallDispatcher__register(SyscallDispatcherPtr self, SystemCall syscall, Handler handler)
{
    ResultMatch(CollectionCode, Handlers__set(&self->handlers, syscall, handler));
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
                                                     KernelPtr kernel,
                                                     Optional(ProcessPtr) process_opt,
                                                     SystemCall syscall_id,
                                                     Anything arg1,
                                                     Anything arg2,
                                                     Anything arg3,
                                                     Anything arg4,
                                                     Anything arg5)
{
    __d_printf(DEBUG, "SyscallHandler__handle(syscall_id=%i, arg1=%i, arg2=%i, arg3=%i, arg4=%i, arg5=%i)",
               syscall_id, arg1, arg2, arg3, arg4, arg5);

    Match(Handler, Handlers__at(&self->handlers, syscall_id));
        IsSome(handler,
        {
            return handler(kernel, process_opt, arg1, arg2, arg3, arg4, arg5);
        });
        IsNone(
        {
            return Error(SystemCallResult, unhandled_syscall, SYSCALL_FAILURE);
        })
    End;

    return Error(SystemCallResult, KString__sprintf(&unhandled_syscall_format, syscall_id), SYSCALL_FAILURE);
}