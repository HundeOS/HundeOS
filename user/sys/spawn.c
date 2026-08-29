#include "spawn.h"

#include "syscall.h"
#include "syscalls/calls.h"

static BString spawn_success = bstr("spawn ok");
static BString spawn_no_mem = bstr("spawn failed: no memory");
static BString spawn_no_file = bstr("spawn failed: file not found");
static BString spawn_no_path = bstr("spawn failed: path not found");
static BString spawn_arg_too_long = bstr("spawn failed: argument too long");
static BString spawn_failed = bstr("spawn failed");

BString SpawnResult__message(SpawnResult result)
{
    switch (result)
    {
        case SPAWN_SUCCESS:
            return spawn_success;
        case SPAWN_NO_MEM:
            return spawn_no_mem;
        case SPAWN_NO_FILE:
            return spawn_no_file;
        case SPAWN_NO_PATH:
            return spawn_no_path;
        case SPAWN_ARG_TOO_LONG:
            return spawn_arg_too_long;
        default:
            return spawn_failed;
    }
}

Result(SpawnResult) SpawnResult__from_raw(SpawnResult result)
{
    if (result >= 0)
    {
        return Success(SpawnResult, result);
    }

    return Error(SpawnResult, SpawnResult__message(result), result);
}

SpawnResult spawn(
    SpawnMode mode,
    ConstBStringPtr path,
    Optional(BString) arg0,
    Optional(BString) arg1,
    Optional(BString) arg2
)
{
    SpawnRequest request = SpawnRequest__new(
        mode,
        path,
        SPAWN_INHERIT_USER_ID,
        SPAWN_INHERIT_AUTH_TICKET,
        SPAWN_STDIO_DEFAULT,
        arg0,
        arg1,
        arg2
    );
    return (SpawnResult) syscall(SYSCALL_EXEC, (Anything) &request, 0, 0, 0, 0);
}

SpawnResult spawn_as(
    SpawnMode mode,
    UserID requested_user_id,
    ConstBStringPtr path,
    Optional(BString) arg0,
    Optional(BString) arg1,
    Optional(BString) arg2
)
{
    SpawnRequest request = SpawnRequest__new(
        mode,
        path,
        requested_user_id,
        SPAWN_INHERIT_AUTH_TICKET,
        SPAWN_STDIO_DEFAULT,
        arg0,
        arg1,
        arg2
    );
    return (SpawnResult) syscall(SYSCALL_EXEC_REQUEST, (Anything) &request, 0, 0, 0, 0);
}

Result(SpawnResult) OS__spawn(
    SpawnMode mode,
    ConstBStringPtr path,
    Optional(BString) arg0,
    Optional(BString) arg1,
    Optional(BString) arg2
)
{
    return SpawnResult__from_raw(spawn(mode, path, arg0, arg1, arg2));
}

Result(SpawnResult) OS__spawn_as(
    SpawnMode mode,
    UserID requested_user_id,
    ConstBStringPtr path,
    Optional(BString) arg0,
    Optional(BString) arg1,
    Optional(BString) arg2
)
{
    return SpawnResult__from_raw(spawn_as(mode, requested_user_id, path, arg0, arg1, arg2));
}

Result(SpawnResult) OS__spawn_with_auth(
    SpawnMode mode,
    AuthTicketID auth_ticket_id,
    ConstBStringPtr path,
    Optional(BString) arg0,
    Optional(BString) arg1,
    Optional(BString) arg2
)
{
    return SpawnResult__from_raw(spawn_with_auth(mode, auth_ticket_id, path, arg0, arg1, arg2));
}

Result(SpawnResult) OS__spawn_with_stdio(
    SpawnMode mode,
    ConstBStringPtr path,
    SpawnFileDescriptor stdin_fd,
    SpawnFileDescriptor stdout_fd,
    SpawnFileDescriptor stderr_fd,
    Optional(BString) arg0,
    Optional(BString) arg1,
    Optional(BString) arg2
)
{
    return SpawnResult__from_raw(spawn_with_stdio(mode, path, stdin_fd, stdout_fd, stderr_fd, arg0, arg1, arg2));
}

SpawnResult spawn_with_auth(
    SpawnMode mode,
    AuthTicketID auth_ticket_id,
    ConstBStringPtr path,
    Optional(BString) arg0,
    Optional(BString) arg1,
    Optional(BString) arg2
)
{
    SpawnRequest request = SpawnRequest__new(
        mode,
        path,
        SPAWN_INHERIT_USER_ID,
        auth_ticket_id,
        SPAWN_STDIO_DEFAULT,
        arg0,
        arg1,
        arg2
    );
    return (SpawnResult) syscall(SYSCALL_EXEC_REQUEST, (Anything) &request, 0, 0, 0, 0);
}

SpawnResult spawn_with_stdio(
    SpawnMode mode,
    ConstBStringPtr path,
    SpawnFileDescriptor stdin_fd,
    SpawnFileDescriptor stdout_fd,
    SpawnFileDescriptor stderr_fd,
    Optional(BString) arg0,
    Optional(BString) arg1,
    Optional(BString) arg2
)
{
    SpawnRequest request = SpawnRequest__new(
        mode,
        path,
        SPAWN_INHERIT_USER_ID,
        SPAWN_INHERIT_AUTH_TICKET,
        SpawnStdio__new(stdin_fd, stdout_fd, stderr_fd),
        arg0,
        arg1,
        arg2
    );
    return (SpawnResult) syscall(SYSCALL_EXEC_REQUEST, (Anything) &request, 0, 0, 0, 0);
}
