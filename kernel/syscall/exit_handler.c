#include "exit_handler.h"

Result(SystemCallResult) Exit__handler(ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3)
{
    k_println("Exiting process...");

    return Success(SystemCallResult, SYSCALL_OK);
}