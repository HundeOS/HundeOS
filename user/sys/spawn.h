#ifndef __USER_SYS_SPAWN_H
#define __USER_SYS_SPAWN_H

#include "syscalls/spawn.h"

SpawnResult spawn(SpawnMode mode, const char* path, const char* arg0, const char* arg1, const char* arg2);
SpawnResult spawn_as(
    SpawnMode mode,
    UserID requested_user_id,
    const char* path,
    const char* arg0,
    const char* arg1,
    const char* arg2
);
SpawnResult spawn_with_auth(
    SpawnMode mode,
    AuthTicketID auth_ticket_id,
    const char* path,
    const char* arg0,
    const char* arg1,
    const char* arg2
);
SpawnResult spawn_with_stdio(
    SpawnMode mode,
    const char* path,
    SpawnFileDescriptor stdin_fd,
    SpawnFileDescriptor stdout_fd,
    SpawnFileDescriptor stderr_fd,
    const char* arg0,
    const char* arg1,
    const char* arg2
);


#endif
