#include "exec_handler.h"

#include "../../common_includes/syscalls/spawn.h"
#include "../exec/exec.h"
#include "../types/bstring_additional_types.h"

Result(SystemCallResult) Exec__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything arg1,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    BString path = bstring((const char *)arg1);
    __d_printf(EXEC, "exec handler path=%s mode=%i\n", cstr(path), (int) arg2);

    ProcessArguments arguments = ProcessArguments__new();
    if (arg3 != 0)
    {
        ProcessArguments__push(&arguments, bstring((const char *) arg3));
    }
    if (arg4 != 0)
    {
        ProcessArguments__push(&arguments, bstring((const char *) arg4));
    }
    if (arg5 != 0)
    {
        ProcessArguments__push(&arguments, bstring((const char *) arg5));
    }
    CallContext call_context = CallContext__new(proc, EXEC, arg1, arg2, arg3, arg4, arg5);
    OriginatingContext originating_context = OriginatingContext__syscall(kernel, call_context);

    SpawnMode mode = (SpawnMode) arg2;
    bool wait = mode == P_WAIT;
    Result(ProcessPtr) exec_res = Exec__exec(originating_context, path, wait, arguments);
    ResultMatch(ProcessPtr, exec_res)
        IsSuccess(proc,
        {
            __d_printf(EXEC, "exec handler success path=%s pid=%i\n", cstr(path), proc->pid);
            return Success(SystemCallResult, proc->pid);
        })
        IsError(code, message,
        {
            __d_printf(EXEC, "exec handler error path=%s code=%i message=%s\n", cstr(path), code, cstr(message));
            return Error(SystemCallResult, message, code);
        })
    End

    return Error(SystemCallResult, bstring("Exec handler failed"), SPAWN_FAILED);
}

Result(SystemCallResult) ExecRequest__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything arg1,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, bstring("Invalid exec request"), SPAWN_FAILED);
    }

    SpawnRequestPtr request = (SpawnRequestPtr) arg1;
    if (request->path == 0)
    {
        return Error(SystemCallResult, bstring("Invalid exec request"), SPAWN_FAILED);
    }

    BString path = bstring(request->path);
    __d_printf(EXEC, "exec request handler path=%s mode=%i requested_user=%i\n", cstr(path), request->mode, (int) request->requested_user_id);

    ProcessArguments arguments = ProcessArguments__new();
    if (request->arg0 != 0)
    {
        ProcessArguments__push(&arguments, bstring(request->arg0));
    }
    if (request->arg1 != 0)
    {
        ProcessArguments__push(&arguments, bstring(request->arg1));
    }
    if (request->arg2 != 0)
    {
        ProcessArguments__push(&arguments, bstring(request->arg2));
    }

    CallContext call_context = CallContext__new(proc, EXEC, (Anything) request->path, request->mode, (Anything) request->arg0, (Anything) request->arg1, (Anything) request->arg2);
    OriginatingContext originating_context = OriginatingContext__syscall(kernel, call_context);

    SpawnMode mode = request->mode;
    bool wait = mode == P_WAIT;
    Optional(UserID) requested_user_id = request->requested_user_id == SPAWN_INHERIT_USER_ID
        ? None(UserID)
        : Some(UserID, request->requested_user_id);

    Result(ProcessPtr) exec_res = Exec__exec_as(originating_context, path, wait, requested_user_id, arguments);
    ResultMatch(ProcessPtr, exec_res)
        IsSuccess(spawned_proc,
        {
            __d_printf(EXEC, "exec request handler success path=%s pid=%i\n", cstr(path), spawned_proc->pid);
            return Success(SystemCallResult, spawned_proc->pid);
        })
        IsError(code, message,
        {
            __d_printf(EXEC, "exec request handler error path=%s code=%i message=%s\n", cstr(path), code, cstr(message));
            return Error(SystemCallResult, message, code);
        })
    End

    return Error(SystemCallResult, bstring("Exec request handler failed"), SPAWN_FAILED);
}
