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

FixedArrayBodyDefine(Handlers);

static BString unhandled_syscall = BSTRING_NEW("Unhandled syscall");
static BString unhandled_syscall_format = BSTRING_NEW("Unhandled syscall %i");

static Result(SystemCallResult) SyscallDispatcher__deliver_pending_signal(
    KernelPtr kernel, Optional(ProcessPtr) process_opt, Result(SystemCallResult) syscall_result
)
{
    Match(ProcessPtr, process_opt)
        IsSome(process,
        {
            Match(ProcessSignal, Process__take_pending_signal(process))
                IsSome(signal,
                {
                    Process__dispose_owned_kernel_resources(kernel, process);
                    Scheduler__exit(kernel->scheduler, process, ProcessSignal__exit_code(signal));
                })
            End
        })
    End

    return syscall_result;
}


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
            return SyscallDispatcher__deliver_pending_signal(
                kernel, process_opt, handler(kernel, process_opt, arg1, arg2, arg3, arg4, arg5)
            );
        });
        IsNone(
        {
            return Error(SystemCallResult, unhandled_syscall, SYSCALL_FAILURE);
        })
    End;

    return Error(SystemCallResult, BString__sprintf(&unhandled_syscall_format, syscall_id), SYSCALL_FAILURE);
}
