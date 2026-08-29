#include "shutdown_handler.h"

#include "../arch/system.h"

static BString shutdown_returned = bstr("Shutdown returned unexpectedly");

Result(SystemCallResult) Shutdown__handler(
    KernelPtr kernel,
    Optional(ProcessPtr) proc,
    Anything arg1,
    Anything arg2,
    Anything arg3,
    Anything arg4,
    Anything arg5
)
{
    System__shutdown();
    return Error(SystemCallResult, shutdown_returned, SYSCALL_FAILURE);
}
