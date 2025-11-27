#include "exit_handler.h"

#include "../kernel_core.h"
#include "syscalls/calls.h"

static KString process_not_found = KSTRING_NEW("Process not found");

Result(SystemCallResult) Exit__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything arg1,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    ReturnCode exit_code = (ReturnCode) arg1;
    Match(ProcessPtr, proc)
        IsSome(process,
        {
            __d_printf(SYSCALL, "Exiting process %i %s", process->pid, cstr(process->name));

            Scheduler__exit(kernel->scheduler, process, exit_code);
            return Success(SystemCallResult, SYSCALL_OK);
        })
        IsNone(return Error(SystemCallResult, process_not_found, SYSCALL_FAILURE))
    End
}