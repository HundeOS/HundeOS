#include "exit_handler.h"

#include "../kernel_core.h"
#include "syscalls/calls.h"

static BString process_not_found = bstr("Process not found");

Result(SystemCallResult) Exit__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything arg1,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    ReturnCode exit_code = (ReturnCode) arg1;
    match_optional(ProcessPtr, proc,
        some(process)
        {
            __d_printf(SYSCALL, "Exiting process %i %s", process->pid, cstr(process->name));

            Process__dispose_owned_kernel_resources(process);
            Scheduler__exit(kernel->scheduler, process, exit_code);
            return Success(SystemCallResult, SYSCALL_OK);
        },
        none() { return Error(SystemCallResult, process_not_found, SYSCALL_FAILURE); }
    );
}
