#include "../../kernel/arch/syscall.h"
#include "process.h"

void SyscallResult__set(ProcessPtr proc, SystemCallResult res)
{
    proc->context->x[0] = res;
}