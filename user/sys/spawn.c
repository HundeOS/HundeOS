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
        .auth_ticket_id = SPAWN_INHERIT_AUTH_TICKET,
        .stdio = SPAWN_STDIO_DEFAULT,
        .arg0 = arg0,
        .arg1 = arg1,
        .arg2 = arg2,
    };
    return (SpawnResult) syscall(SYSCALL_EXEC_REQUEST, (Anything) &request, 0, 0, 0, 0);
}

SpawnResult spawn_with_auth(
    SpawnMode mode,
    AuthTicketID auth_ticket_id,
    const char* path,
    const char* arg0,
    const char* arg1,
    const char* arg2
)
{
    SpawnRequest request = (SpawnRequest) {
        .path = path,
        .mode = mode,
        .requested_user_id = SPAWN_INHERIT_USER_ID,
        .auth_ticket_id = auth_ticket_id,
        .stdio = SPAWN_STDIO_DEFAULT,
        .arg0 = arg0,
        .arg1 = arg1,
        .arg2 = arg2,
    };
    return (SpawnResult) syscall(SYSCALL_EXEC_REQUEST, (Anything) &request, 0, 0, 0, 0);
}

SpawnResult spawn_with_stdio(
    SpawnMode mode,
    const char* path,
    SpawnFileDescriptor stdin_fd,
    SpawnFileDescriptor stdout_fd,
    SpawnFileDescriptor stderr_fd,
    const char* arg0,
    const char* arg1,
    const char* arg2
)
{
    SpawnRequest request = (SpawnRequest) {
        .path = path,
        .mode = mode,
        .requested_user_id = SPAWN_INHERIT_USER_ID,
        .auth_ticket_id = SPAWN_INHERIT_AUTH_TICKET,
        .stdio = SpawnStdio__new(stdin_fd, stdout_fd, stderr_fd),
        .arg0 = arg0,
        .arg1 = arg1,
        .arg2 = arg2,
    };
    return (SpawnResult) syscall(SYSCALL_EXEC_REQUEST, (Anything) &request, 0, 0, 0, 0);
}
