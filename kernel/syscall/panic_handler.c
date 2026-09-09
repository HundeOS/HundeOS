#include "panic_handler.h"

#include "../panic.h"

static BString panic_returned = bstr("Panic returned unexpectedly");

Result(SystemCallResult) Panic__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                        Anything arg3, Anything arg4, Anything arg5)
{
    Kernel__panic("panic syscall requested", NULL);
    return Error(SystemCallResult, panic_returned, SYSCALL_FAILURE);
}
