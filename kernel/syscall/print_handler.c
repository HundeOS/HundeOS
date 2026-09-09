#include "print_handler.h"
#include "../print.h"

Result(SystemCallResult) Print__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                        Anything arg3, Anything arg4, Anything arg5)
{
    k_printf(str(arg1));

    return Success(SystemCallResult, SYSCALL_OK);
}
