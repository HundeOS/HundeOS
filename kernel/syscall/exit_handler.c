#include "exit_handler.h"

Result(SystemCallResult) Exit__handler(
        KernelPtr kernel, ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5
    )
{
    k_println("Exiting process...");

    return Success(SystemCallResult, SYSCALL_OK);
}