#include "spawn.h"

#include "syscall.h"
#include "syscalls/calls.h"

SpawnResult spawn(SpawnMode mode, const char* path, const char* arg0, ...)
{
    SpawnResult result = syscall(SYSCALL_EXEC, (Anything) path, mode, 0, 0, 0);
    return result;
}
