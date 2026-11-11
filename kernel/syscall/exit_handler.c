#include "exit_handler.h"

#include "../kernel_core.h"
#include "syscalls/calls.h"

static BString process_not_found = bstr("Process not found");

Result(SystemCallResult)
    Exit__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                  Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    ReturnCode exit_code = (ReturnCode)arg1;
    match_optional(
        *proc,
        some(process)
        {
            __d_printf(SYSCALL, "Exiting process %i %s", process->pid, cstr(process->name));

            match_result(
                Scheduler__exit(kernel->scheduler, process, exit_code),
                success(exited_pid)
                {
                    return Success(SystemCallResult, SYSCALL_OK);
                },
                error(code, message)
                {
                    return Error(SystemCallResult, message, code);
                }
            );
        },
        none()
        {
            return Error(SystemCallResult, process_not_found, SYSCALL_FAILURE);
        }
    );
}
