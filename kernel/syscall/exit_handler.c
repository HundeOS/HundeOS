#include "exit_handler.h"

#include "../kernel_core.h"

Result(SystemCallResult) Exit__handler(
        KernelPtr kernel, ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5
    )
{
    __d_printf(SYSCALL, "Exiting process %i %s", proc->pid, cstr(proc->name));

    Scheduler__exit(&kernel->scheduler, proc);
    return Success(SystemCallResult, SYSCALL_OK);
}