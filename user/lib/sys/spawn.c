#include "spawn.h"

#include "../../user.h"
#include "syscall.h"
#include "syscalls/calls.h"

#ifndef FIXED_ARRAY_ALLOC
#define FIXED_ARRAY_ALLOC(size) malloc(size)
#define FIXED_ARRAY_ERROR(message) BString__new(message)
#define __USER_SYS_SPAWN_FIXED_ARRAY_ALLOC_DEFINED
#endif

FixedArrayBodyDefine(SpawnArguments);

#ifdef __USER_SYS_SPAWN_FIXED_ARRAY_ALLOC_DEFINED
#undef FIXED_ARRAY_ALLOC
#undef FIXED_ARRAY_ERROR
#undef __USER_SYS_SPAWN_FIXED_ARRAY_ALLOC_DEFINED
#endif

static BString spawn_success = bstr("spawn ok");
static BString spawn_no_mem = bstr("spawn failed: no memory");
static BString spawn_no_file = bstr("spawn failed: file not found");
static BString spawn_no_path = bstr("spawn failed: path not found");
static BString spawn_arg_too_long = bstr("spawn failed: argument too long");
static BString spawn_failed = bstr("spawn failed");

static SpawnResult SpawnRequest__submit(SpawnRequestPtr self, SystemCall operation)
{
    return syscall(operation, (Anything)self, 0, 0, 0, 0);
}

Result(SpawnResult) OS__boot_spawn(ConstBStringPtr path, OptionalPtr(BString) arg0, OptionalPtr(BString) arg1,
                                   OptionalPtr(BString) arg2)
{
    SpawnArguments arguments = SpawnArguments__from_optional_args(arg0, arg1, arg2);
    SpawnRequest request = SpawnRequest__new(P_NOWAIT, path, SPAWN_INHERIT_USER_ID, SPAWN_INHERIT_AUTH_TICKET,
                                             SPAWN_STDIO_DEFAULT, &arguments);
    return SpawnResult__from_raw(syscall(SYSCALL_BOOT_SPAWN, (Anything)&request, 0, 0, 0, 0));
}

Result(SpawnResult) OS__boot_complete(void)
{
    return SpawnResult__from_raw(syscall(SYSCALL_BOOT_COMPLETE, 0, 0, 0, 0, 0));
}

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

static Result(CollectionCode) SpawnArguments__push_optional(SpawnArgumentsPtr self, OptionalPtr(BString) arg)
{
    match_optional(*arg,
        some(value)
        {
            return SpawnArguments__push(self, value);
        },
        none_do_nothing()
    );

    return Success(CollectionCode, 0);
}

SpawnArguments SpawnArguments__from_optional_args(OptionalPtr(BString) arg0, OptionalPtr(BString) arg1,
                                                  OptionalPtr(BString) arg2)
{
    SpawnArguments arguments = SpawnArguments__new();
    match_result(
        SpawnArguments__push_optional(&arguments, arg0),
        success_do_nothing(),
        error(code, message)
        {
            return arguments;
        }
    );
    match_result(
        SpawnArguments__push_optional(&arguments, arg1),
        success_do_nothing(),
        error(code, message)
        {
            return arguments;
        }
    );
    match_result(
        SpawnArguments__push_optional(&arguments, arg2),
        success_do_nothing(),
        error(code, message)
        {
            return arguments;
        }
    );

    return arguments;
}

SpawnResult spawn(SpawnMode mode, ConstBStringPtr path, OptionalPtr(BString) arg0, OptionalPtr(BString) arg1,
                  OptionalPtr(BString) arg2)
{
    SpawnArguments arguments = SpawnArguments__from_optional_args(arg0, arg1, arg2);
    return spawn_argv(mode, path, &arguments);
}

SpawnResult spawn_argv(SpawnMode mode, ConstBStringPtr path, ConstSpawnArgumentsPtr arguments)
{
    SpawnRequest request =
        SpawnRequest__new(mode, path, SPAWN_INHERIT_USER_ID, SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT, arguments);
    return SpawnRequest__submit(&request, SYSCALL_EXEC);
}

SpawnResult spawn_as(SpawnMode mode, UserID requested_user_id, ConstBStringPtr path, OptionalPtr(BString) arg0,
                     OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    SpawnArguments arguments = SpawnArguments__from_optional_args(arg0, arg1, arg2);
    return spawn_as_argv(mode, requested_user_id, path, &arguments);
}

SpawnResult spawn_as_argv(SpawnMode mode, UserID requested_user_id, ConstBStringPtr path,
                          ConstSpawnArgumentsPtr arguments)
{
    SpawnRequest request =
        SpawnRequest__new(mode, path, requested_user_id, SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT, arguments);
    return SpawnRequest__submit(&request, SYSCALL_EXEC_REQUEST);
}

Result(SpawnResult) OS__spawn(SpawnMode mode, ConstBStringPtr path, OptionalPtr(BString) arg0,
                              OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    return SpawnResult__from_raw(spawn(mode, path, arg0, arg1, arg2));
}

Result(SpawnResult) OS__spawn_argv(SpawnMode mode, ConstBStringPtr path, ConstSpawnArgumentsPtr arguments)
{
    return SpawnResult__from_raw(spawn_argv(mode, path, arguments));
}

Result(SpawnResult) OS__spawn_as(SpawnMode mode, UserID requested_user_id, ConstBStringPtr path,
                                 OptionalPtr(BString) arg0, OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    return SpawnResult__from_raw(spawn_as(mode, requested_user_id, path, arg0, arg1, arg2));
}

Result(SpawnResult)
    OS__spawn_as_argv(SpawnMode mode, UserID requested_user_id, ConstBStringPtr path, ConstSpawnArgumentsPtr arguments)
{
    return SpawnResult__from_raw(spawn_as_argv(mode, requested_user_id, path, arguments));
}

Result(SpawnResult) OS__spawn_with_auth(SpawnMode mode, AuthTicketID auth_ticket_id, ConstBStringPtr path,
                                        OptionalPtr(BString) arg0, OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    return SpawnResult__from_raw(spawn_with_auth(mode, auth_ticket_id, path, arg0, arg1, arg2));
}

Result(SpawnResult) OS__spawn_with_auth_argv(SpawnMode mode, AuthTicketID auth_ticket_id, ConstBStringPtr path,
                                             ConstSpawnArgumentsPtr arguments)
{
    return SpawnResult__from_raw(spawn_with_auth_argv(mode, auth_ticket_id, path, arguments));
}

Result(SpawnResult)
    OS__spawn_with_stdio(SpawnMode mode, ConstBStringPtr path, SpawnFileDescriptor stdin_fd,
                         SpawnFileDescriptor stdout_fd, SpawnFileDescriptor stderr_fd, OptionalPtr(BString) arg0,
                         OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    return SpawnResult__from_raw(spawn_with_stdio(mode, path, stdin_fd, stdout_fd, stderr_fd, arg0, arg1, arg2));
}

Result(SpawnResult) OS__spawn_with_stdio_argv(SpawnMode mode, ConstBStringPtr path, SpawnFileDescriptor stdin_fd,
                                              SpawnFileDescriptor stdout_fd, SpawnFileDescriptor stderr_fd,
                                              ConstSpawnArgumentsPtr arguments)
{
    return SpawnResult__from_raw(spawn_with_stdio_argv(mode, path, stdin_fd, stdout_fd, stderr_fd, arguments));
}

SpawnResult spawn_with_auth(SpawnMode mode, AuthTicketID auth_ticket_id, ConstBStringPtr path,
                            OptionalPtr(BString) arg0, OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    SpawnArguments arguments = SpawnArguments__from_optional_args(arg0, arg1, arg2);
    return spawn_with_auth_argv(mode, auth_ticket_id, path, &arguments);
}

SpawnResult spawn_with_auth_report(SpawnMode mode, AuthTicketID auth_ticket_id, ConstBStringPtr path,
                                   SpawnAbortReportPtr abort_report, OptionalPtr(BString) arg0,
                                   OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    SpawnArguments arguments = SpawnArguments__from_optional_args(arg0, arg1, arg2);
    SpawnRequest request =
        SpawnRequest__new(mode, path, SPAWN_INHERIT_USER_ID, auth_ticket_id, SPAWN_STDIO_DEFAULT, &arguments);
    SpawnRequest__set_abort_report(&request, abort_report);
    return SpawnRequest__submit(&request, SYSCALL_EXEC_REQUEST);
}

SpawnResult spawn_with_auth_argv(SpawnMode mode, AuthTicketID auth_ticket_id, ConstBStringPtr path,
                                 ConstSpawnArgumentsPtr arguments)
{
    SpawnRequest request =
        SpawnRequest__new(mode, path, SPAWN_INHERIT_USER_ID, auth_ticket_id, SPAWN_STDIO_DEFAULT, arguments);
    return SpawnRequest__submit(&request, SYSCALL_EXEC_REQUEST);
}

SpawnResult spawn_with_stdio(SpawnMode mode, ConstBStringPtr path, SpawnFileDescriptor stdin_fd,
                             SpawnFileDescriptor stdout_fd, SpawnFileDescriptor stderr_fd, OptionalPtr(BString) arg0,
                             OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    SpawnArguments arguments = SpawnArguments__from_optional_args(arg0, arg1, arg2);
    return spawn_with_stdio_argv(mode, path, stdin_fd, stdout_fd, stderr_fd, &arguments);
}

SpawnResult spawn_with_stdio_argv(SpawnMode mode, ConstBStringPtr path, SpawnFileDescriptor stdin_fd,
                                  SpawnFileDescriptor stdout_fd, SpawnFileDescriptor stderr_fd,
                                  ConstSpawnArgumentsPtr arguments)
{
    SpawnRequest request = SpawnRequest__new(mode, path, SPAWN_INHERIT_USER_ID, SPAWN_INHERIT_AUTH_TICKET,
                                             SpawnStdio__new(stdin_fd, stdout_fd, stderr_fd), arguments);
    return SpawnRequest__submit(&request, SYSCALL_EXEC_REQUEST);
}
