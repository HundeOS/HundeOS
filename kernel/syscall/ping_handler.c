#include "ping_handler.h"

Result(SystemCallResult) Ping__handler(ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3)
{
    k_println("Ping!");
    return Success(SystemCallResult, SYSCALL_OK);
}