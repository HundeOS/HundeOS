#include "exec_handler.h"

#include "../exec/exec.h"
#include "../types/kstring_additional_types.h"

Result(SystemCallResult) Exec__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything arg1,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    KString path = kstring((const char *)arg1);
    Optional(KString) arg0 = None(KString);
    if (arg3 != 0)
    {
        arg0 = Some(KString, kstring((const char *) arg3));
    }
    CallContext call_context = CallContext__new(proc, EXEC, arg1, arg2, arg3, arg4, arg5);
    OriginatingContext originating_context = OriginatingContext__syscall(kernel, call_context);

    Result(ProcessPtr) exec_res = Exec__exec(originating_context, path, arg2 != 0, arg0);
    ResultMatch(ProcessPtr, exec_res)
        IsSuccess(proc,
        {
            return Success(SystemCallResult, proc->pid);
        })
        IsError(code, message,
        {
            return Error(SystemCallResult, message, code);
        })
    End
}
