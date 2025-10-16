#include "print_handler.h"
#include "../print.h"

Result(SystemCallResult) Print__handler(ProcessPtr proc, Anything arg1, Anything arg2, Anything arg3)
{
    k_println(str(arg1));

    return Success(SystemCallResult, SYSCALL_OK);
}