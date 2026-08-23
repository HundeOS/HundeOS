#include "deferred.h"

static BString missing_return_code = BSTRING_NEW("Missing return code");

Result(Anything) SystemCall__Complete(ParkedOperationPtr parked_operation)
{
    match_optional(ReturnCode, parked_operation->return_code,
        some(code) { return Success(Anything, code);; },
        none() {}
    )

    return Error(Anything, missing_return_code, SYSCALL_FAILURE);
}
