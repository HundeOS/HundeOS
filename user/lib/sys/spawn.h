#ifndef __USER_SYS_SPAWN_H
#define __USER_SYS_SPAWN_H

#include "../../common/types/result.h"
#include "syscalls/spawn.h"

ResultDef(SpawnResult);

SpawnResult spawn(
    SpawnMode mode,
    ConstBStringPtr path,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
SpawnResult spawn_argv(
    SpawnMode mode,
    ConstBStringPtr path,
    ConstSpawnArgumentsPtr arguments
);
SpawnResult spawn_as(
    SpawnMode mode,
    UserID requested_user_id,
    ConstBStringPtr path,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
SpawnResult spawn_as_argv(
    SpawnMode mode,
    UserID requested_user_id,
    ConstBStringPtr path,
    ConstSpawnArgumentsPtr arguments
);
SpawnResult spawn_with_auth(
    SpawnMode mode,
    AuthTicketID auth_ticket_id,
    ConstBStringPtr path,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
SpawnResult spawn_with_auth_argv(
    SpawnMode mode,
    AuthTicketID auth_ticket_id,
    ConstBStringPtr path,
    ConstSpawnArgumentsPtr arguments
);
SpawnResult spawn_with_stdio(
    SpawnMode mode,
    ConstBStringPtr path,
    SpawnFileDescriptor stdin_fd,
    SpawnFileDescriptor stdout_fd,
    SpawnFileDescriptor stderr_fd,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
SpawnResult spawn_with_stdio_argv(
    SpawnMode mode,
    ConstBStringPtr path,
    SpawnFileDescriptor stdin_fd,
    SpawnFileDescriptor stdout_fd,
    SpawnFileDescriptor stderr_fd,
    ConstSpawnArgumentsPtr arguments
);

BString SpawnResult__message(SpawnResult result);
Result(SpawnResult) SpawnResult__from_raw(SpawnResult result);
SpawnArguments SpawnArguments__from_optional_args(
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
Result(SpawnResult) OS__spawn(
    SpawnMode mode,
    ConstBStringPtr path,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
Result(SpawnResult) OS__spawn_argv(
    SpawnMode mode,
    ConstBStringPtr path,
    ConstSpawnArgumentsPtr arguments
);
Result(SpawnResult) OS__spawn_as(
    SpawnMode mode,
    UserID requested_user_id,
    ConstBStringPtr path,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
Result(SpawnResult) OS__spawn_as_argv(
    SpawnMode mode,
    UserID requested_user_id,
    ConstBStringPtr path,
    ConstSpawnArgumentsPtr arguments
);
Result(SpawnResult) OS__spawn_with_auth(
    SpawnMode mode,
    AuthTicketID auth_ticket_id,
    ConstBStringPtr path,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
Result(SpawnResult) OS__spawn_with_auth_argv(
    SpawnMode mode,
    AuthTicketID auth_ticket_id,
    ConstBStringPtr path,
    ConstSpawnArgumentsPtr arguments
);
Result(SpawnResult) OS__spawn_with_stdio(
    SpawnMode mode,
    ConstBStringPtr path,
    SpawnFileDescriptor stdin_fd,
    SpawnFileDescriptor stdout_fd,
    SpawnFileDescriptor stderr_fd,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
Result(SpawnResult) OS__spawn_with_stdio_argv(
    SpawnMode mode,
    ConstBStringPtr path,
    SpawnFileDescriptor stdin_fd,
    SpawnFileDescriptor stdout_fd,
    SpawnFileDescriptor stderr_fd,
    ConstSpawnArgumentsPtr arguments
);


#endif
