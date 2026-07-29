#include "deferred.h"

static KString missing_return_code = KSTRING_NEW("Missing return code");

Result(Anything) SystemCall__Complete(ParkedOperationPtr parked_operation)
{
    Match(ReturnCode, parked_operation->return_code)
        IsSome(code, return Success(Anything, code);)
    End

    return Error(Anything, missing_return_code, SYSCALL_FAILURE);
}
