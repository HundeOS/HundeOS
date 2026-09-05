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
SpawnResult spawn_as(
    SpawnMode mode,
    UserID requested_user_id,
    ConstBStringPtr path,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
SpawnResult spawn_with_auth(
    SpawnMode mode,
    AuthTicketID auth_ticket_id,
    ConstBStringPtr path,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
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

BString SpawnResult__message(SpawnResult result);
Result(SpawnResult) SpawnResult__from_raw(SpawnResult result);
Result(SpawnResult) OS__spawn(
    SpawnMode mode,
    ConstBStringPtr path,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
Result(SpawnResult) OS__spawn_as(
    SpawnMode mode,
    UserID requested_user_id,
    ConstBStringPtr path,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
);
Result(SpawnResult) OS__spawn_with_auth(
    SpawnMode mode,
    AuthTicketID auth_ticket_id,
    ConstBStringPtr path,
    OptionalPtr(BString) arg0,
    OptionalPtr(BString) arg1,
    OptionalPtr(BString) arg2
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


#endif
