#include "syscall.h"

CallContext CallContext__new(OptionalPtr(ProcessPtr) process, SystemCall syscall_id, Anything arg1, Anything arg2,
                             Anything arg3, Anything arg4, Anything arg5)
{
    return (CallContext){.process_opt = *process,
                         .syscall_id = syscall_id,
                         .arg1 = arg1,
                         .arg2 = arg2,
                         .arg3 = arg3,
                         .arg4 = arg4,
                         .arg5 = arg5};
}

bool SystemCallResult__restores_context(SystemCall syscall_id, SystemCallResult result)
{
    return syscall_id == SYSCALL_PROCESS_SIGNAL_RETURN && result == SYSCALL_CONTEXT_RESTORED;
}
