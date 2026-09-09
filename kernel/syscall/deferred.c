#include "deferred.h"

static BString missing_return_code = bstr("Missing return code");

Result(Anything) SystemCall__Complete(ParkedOperationPtr parked_operation)
{
    match_optional(
        parked_operation->return_code,
        some(code)
        {
            return Success(Anything, code);
            ;
        },
        none_do_nothing()
    );

    return Error(Anything, missing_return_code, SYSCALL_FAILURE);
}
