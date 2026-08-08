#include "spawn.h"

#include "syscall.h"
#include "syscalls/calls.h"

SpawnResult spawn(SpawnMode mode, const char* path, const char* arg0, const char* arg1, const char* arg2)
{
    SpawnResult result = syscall(SYSCALL_EXEC, (Anything) path, mode, (Anything) arg0, (Anything) arg1, (Anything) arg2);
    return result;
}

SpawnResult spawn_as(
    SpawnMode mode,
    UserID requested_user_id,
    const char* path,
    const char* arg0,
    const char* arg1,
    const char* arg2
)
{
    SpawnRequest request = (SpawnRequest) {
        .path = path,
        .mode = mode,
        .requested_user_id = requested_user_id,
        .arg0 = arg0,
        .arg1 = arg1,
        .arg2 = arg2,
    };
    return (SpawnResult) syscall(SYSCALL_EXEC_REQUEST, (Anything) &request, 0, 0, 0, 0);
}
