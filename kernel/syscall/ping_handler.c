#include "ping_handler.h"

Result(SystemCallResult) Ping__handler(
    KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    k_println("Ping!");
    return Success(SystemCallResult, SYSCALL_OK);
}
