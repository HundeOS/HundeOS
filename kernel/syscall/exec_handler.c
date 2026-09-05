#include "exec_handler.h"

#include "../../common_includes/syscalls/spawn.h"
#include "../exec/exec.h"
#include "../types/bstring_additional_types.h"
#include "auth.h"

static void ExecRequest__push_arg(ProcessArgumentsPtr arguments, OptionalPtr(BString) arg)
{
    match_optional(BString, *arg,
        some(value) { ProcessArguments__push(arguments, value); },
        none_do_nothing()
    );
}

Result(SystemCallResult) Exec__handler(
    KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    return ExecRequest__handler(kernel, proc, arg1, arg2, arg3, arg4, arg5);
}


Result(SystemCallResult) ExecRequest__handler(
    KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, bstring("Invalid exec request"), SPAWN_FAILED);
    }

    SpawnRequestPtr request = (SpawnRequestPtr) arg1;

    __d_printf(
        EXEC,
        "exec request handler path=%s mode=%i requested_user=%i auth_ticket=%i\n",
        cstr(request->path),
        request->mode,
        (int) request->requested_user_id,
        (int) request->auth_ticket_id
    );

    ProcessArguments arguments = ProcessArguments__new();
    ExecRequest__push_arg(&arguments, &request->arg0);
    ExecRequest__push_arg(&arguments, &request->arg1);
    ExecRequest__push_arg(&arguments, &request->arg2);

    CallContext call_context = CallContext__new(proc, EXEC, arg1, arg2, arg3, arg4, arg5);
    OriginatingContext originating_context = OriginatingContext__syscall(kernel, &call_context);

    SpawnMode mode = request->mode;
    bool wait = mode == P_WAIT;
    Optional(UserID) requested_user_id = None(UserID);
    if (request->auth_ticket_id != SPAWN_INHERIT_AUTH_TICKET)
    {
        match_optional(ProcessPtr, *proc,
            some(process)
            {
                match_result(UserID, Auth__consume_ticket(&kernel->auth, process, request->auth_ticket_id),
                    success(user_id) { requested_user_id = Some(UserID, user_id);; },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            },
            none() { return Error(SystemCallResult, bstring("No process"), AUTH_NO_PROCESS); }
        );
    }

    Result(ProcessPtr) exec_res = Exec__exec_authorized_as_with_stdio(
        &originating_context, &request->path, wait, &requested_user_id, &arguments, &request->stdio
    );
    match_result(ProcessPtr, exec_res,
        success(spawned_proc)
        {
            __d_printf(EXEC, "exec request handler success path=%s pid=%i\n", cstr(request->path), spawned_proc->pid);
            return Success(SystemCallResult, spawned_proc->pid);
        },
        error(code, message)
        {
            __d_printf(EXEC, "exec request handler error path=%s code=%i message=%s\n", cstr(request->path), code, cstr(message));
            return Error(SystemCallResult, message, code);
        }
    );

    return Error(SystemCallResult, bstring("Exec request handler failed"), SPAWN_FAILED);
}
