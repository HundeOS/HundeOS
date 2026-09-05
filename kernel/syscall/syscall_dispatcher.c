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

static BString unhandled_syscall = bstr("Unhandled syscall");
static BString unhandled_syscall_format = bstr("Unhandled syscall %i");

static Result(SystemCallResult) SyscallDispatcher__deliver_pending_signal(
    KernelPtr kernel, OptionalPtr(ProcessPtr) process_opt, ConstResultPtr(SystemCallResult) syscall_result
)
{
    match_optional(ProcessPtr, *process_opt,
        some(process)
        {
            match_optional(ProcessSignal, Process__take_pending_signal(process),
                some(signal)
                {
                    Process__dispose_owned_kernel_resources(process);
                    Scheduler__exit(kernel->scheduler, process, ProcessSignal__exit_code(signal));
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    return *syscall_result;
}


Result(RegisterResult) SyscallDispatcher__register(SyscallDispatcherPtr self, SystemCall syscall, Handler handler)
{
    match_result(CollectionCode, Handlers__set(&self->handlers, syscall, handler),
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

Result(SystemCallResult) SyscallDispatcher__dispatch(SyscallDispatcherPtr self,
                                                              KernelPtr kernel,
                                                              OptionalPtr(ProcessPtr) process_opt,
                                                              SystemCall syscall_id,
                                                              Anything arg1,
                                                              Anything arg2,
                                                              Anything arg3,
                                                              Anything arg4,
                                                              Anything arg5)
{
    __d_printf(DEBUG, "SyscallHandler__handle(syscall_id=%i, arg1=%i, arg2=%i, arg3=%i, arg4=%i, arg5=%i)",
               syscall_id, arg1, arg2, arg3, arg4, arg5);

    match_optional(Handler, Handlers__at(&self->handlers, syscall_id),
        some(handler)
        {
            Result(SystemCallResult) handler_result = handler(kernel, process_opt, arg1, arg2, arg3, arg4, arg5);
            return SyscallDispatcher__deliver_pending_signal(
                kernel, process_opt, &handler_result
            );
        },
        none()
        {
            return Error(SystemCallResult, unhandled_syscall, SYSCALL_FAILURE);
        }
    );

    return Error(SystemCallResult, BString__sprintf(&unhandled_syscall_format, syscall_id), SYSCALL_FAILURE);
}
