#include "deferred.h"

static KString missing_return_code = KSTRING_NEW("Missing return code");

Result(Anything) SystemCall__Complete(ParkedKernelContextPtr parked_kernel_context)
{
    Match(ReturnCode, parked_kernel_context->return_code)
        IsSome(code, return Success(Anything, code);)
    End

    return Error(Anything, missing_return_code, SYSCALL_FAILURE);
}