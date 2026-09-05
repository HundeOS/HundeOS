#ifndef __SYSCALL_SPAWN_H
#define __SYSCALL_SPAWN_H
#include <stdbool.h>
#include <stdint.h>

#include "auth.h"
#include "process.h"
#include "types/bstring.h"

#define SPAWN_MAX_ARGS 3
#define SPAWN_INHERIT_USER_ID UINT64_MAX
#define SPAWN_INHERIT_AUTH_TICKET AUTH_NO_TICKET
#define SPAWN_STDIO_DEFAULT ((SpawnStdio) { 0 })
#define SPAWN_NO_ARG None(BString)

typedef enum SpawnModes
{
    P_OVERLAY = 1,	// Overlays parent process with child, which destroys the parent. This has the same effect as the exec* functions.
    P_WAIT = 2,	    // Suspends parent process until the child process has finished executing (synchronous spawn).
    P_NOWAIT = 3,   //
    P_NOWAITO = 4,	// Continues to execute calling process concurrently with new process (asynchronous spawn).
    P_DETACH = 5,	// The child is run in background without access to the console or keyboard. Calls to _cwait upon the new process will fail (asynchronous spawn)
}
SpawnModes;

typedef enum SpawnErrors
{
    SPAWN_SUCCESS = 0,
    SPAWN_NO_MEM = -1,
    SPAWN_NO_FILE = -2,
    SPAWN_NO_PATH = -3,
    SPAWN_ARG_TOO_LONG = -4,
    SPAWN_FAILED = -128,
}
SpawnErrors;

typedef int64_t SpawnResult;
typedef int SpawnMode;
typedef uint64_t SpawnFileDescriptor;

typedef struct SpawnStdio
{
    uint8_t present;
    SpawnFileDescriptor stdin_fd;
    SpawnFileDescriptor stdout_fd;
    SpawnFileDescriptor stderr_fd;
} SpawnStdio;
typedef const SpawnStdio * ConstSpawnStdioPtr;

static inline SpawnStdio SpawnStdio__new(
    SpawnFileDescriptor stdin_fd,
    SpawnFileDescriptor stdout_fd,
    SpawnFileDescriptor stderr_fd
)
{
    return (SpawnStdio) {
        .present = 1,
        .stdin_fd = stdin_fd,
        .stdout_fd = stdout_fd,
        .stderr_fd = stderr_fd,
    };
}

static inline bool SpawnStdio__is_present(SpawnStdio self)
{
    return self.present != 0;
}

static inline SpawnFileDescriptor SpawnStdio__stdin_fd(SpawnStdio self)
{
    return self.stdin_fd;
}

static inline SpawnFileDescriptor SpawnStdio__stdout_fd(SpawnStdio self)
{
    return self.stdout_fd;
}

static inline SpawnFileDescriptor SpawnStdio__stderr_fd(SpawnStdio self)
{
    return self.stderr_fd;
}

typedef struct SpawnRequest
{
    BString path;
    SpawnMode mode;
    UserID requested_user_id;
    AuthTicketID auth_ticket_id;
    SpawnStdio stdio;
    Optional(BString) arg0;
    Optional(BString) arg1;
    Optional(BString) arg2;
} SpawnRequest;

typedef SpawnRequest * SpawnRequestPtr;

static inline Optional(BString) SpawnArg__from_bstring(ConstBStringPtr value)
{
    if (BString__is_empty(value))
    {
        return None(BString);
    }

    return Some(BString, *value);
}

static inline SpawnRequest SpawnRequest__new(
    SpawnMode mode,
    ConstBStringPtr path,
    UserID requested_user_id,
    AuthTicketID auth_ticket_id,
    SpawnStdio stdio,
    Optional(BString) arg0,
    Optional(BString) arg1,
    Optional(BString) arg2
)
{
    return (SpawnRequest) {
        .path = *path,
        .mode = mode,
        .requested_user_id = requested_user_id,
        .auth_ticket_id = auth_ticket_id,
        .stdio = stdio,
        .arg0 = arg0,
        .arg1 = arg1,
        .arg2 = arg2,
    };
}

#endif
