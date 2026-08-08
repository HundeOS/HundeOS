#include "deferred.h"

static BString missing_return_code = BSTRING_NEW("Missing return code");

Result(Anything) SystemCall__Complete(ParkedOperationPtr parked_operation)
{
    Match(ReturnCode, parked_operation->return_code)
        IsSome(code, return Success(Anything, code);)
    End

    return Error(Anything, missing_return_code, SYSCALL_FAILURE);
}
