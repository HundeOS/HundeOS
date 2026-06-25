#include "spawn.h"

#include "syscall.h"
#include "syscalls/calls.h"

SpawnResult spawn(SpawnMode mode, const char* path, const char* arg0, const char* arg1, const char* arg2)
{
    SpawnResult result = syscall(SYSCALL_EXEC, (Anything) path, mode, (Anything) arg0, (Anything) arg1, (Anything) arg2);
    return result;
}
