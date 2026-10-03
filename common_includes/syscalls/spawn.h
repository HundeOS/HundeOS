#ifndef __SYSCALL_SPAWN_H
#define __SYSCALL_SPAWN_H
#include <stdbool.h>
#include <stdint.h>

#include "auth.h"
#include "exec.h"
#include "process.h"
#include "types/bstring.h"

#ifndef FIXED_ARRAY_ALLOC
#define FIXED_ARRAY_ALLOC(size) ((void *)0)
#define __SPAWN_FIXED_ARRAY_ALLOC_DEFINED
#endif

#include "../../common/types/fixed_array.h"

#ifdef __SPAWN_FIXED_ARRAY_ALLOC_DEFINED
#undef FIXED_ARRAY_ALLOC
#undef __SPAWN_FIXED_ARRAY_ALLOC_DEFINED
#endif

#define SPAWN_MAX_ARGS 16
#define SPAWN_INHERIT_USER_ID UINT64_MAX
#define SPAWN_INHERIT_AUTH_TICKET AUTH_NO_TICKET
#define SPAWN_STDIO_DEFAULT ((SpawnStdio){0})
#define SPAWN_NO_ARG None(BString)

FixedArrayDefine(BString, SPAWN_MAX_ARGS, SpawnArguments, bstring_equals);
typedef SpawnArguments * SpawnArgumentsPtr;
typedef const SpawnArguments * ConstSpawnArgumentsPtr;

typedef enum SpawnModes
{
    P_OVERLAY =
        1, // Overlays parent process with child, which destroys the parent. This has the same effect as the exec* functions.
    P_WAIT = 2,    // Suspends parent process until the child process has finished executing (synchronous spawn).
    P_NOWAIT = 3,  //
    P_NOWAITO = 4, // Continues to execute calling process concurrently with new process (asynchronous spawn).
    P_DETACH =
        5, // The child is run in background without access to the console or keyboard. Calls to _cwait upon the new process will fail (asynchronous spawn)
} SpawnModes;

typedef enum SpawnErrors
{
    SPAWN_SUCCESS = 0,
    SPAWN_NO_MEM = -1,
    SPAWN_NO_FILE = -2,
    SPAWN_NO_PATH = -3,
    SPAWN_ARG_TOO_LONG = -4,
    SPAWN_PENDING = -5,
    SPAWN_FAILED = -128,
} SpawnErrors;

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

static inline SpawnStdio SpawnStdio__new(SpawnFileDescriptor stdin_fd, SpawnFileDescriptor stdout_fd,
                                         SpawnFileDescriptor stderr_fd)
{
    return (SpawnStdio){
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
    SpawnArguments arguments;
    uint64_t overlay_payload;
    uint64_t overlay_payload_size;
} SpawnRequest;

typedef SpawnRequest * SpawnRequestPtr;
typedef const SpawnRequest * ConstSpawnRequestPtr;

static inline Optional(BString) SpawnArg__from_bstring(ConstBStringPtr value)
{
    if (BString__is_empty(value))
    {
        return None(BString);
    }

    return Some(BString, *value);
}

static inline SpawnRequest SpawnRequest__new(SpawnMode mode, ConstBStringPtr path, UserID requested_user_id,
                                             AuthTicketID auth_ticket_id, SpawnStdio stdio,
                                             ConstSpawnArgumentsPtr arguments)
{
    return (SpawnRequest){
        .path = *path,
        .mode = mode,
        .requested_user_id = requested_user_id,
        .auth_ticket_id = auth_ticket_id,
        .stdio = stdio,
        .arguments = *arguments,
        .overlay_payload = 0,
        .overlay_payload_size = 0,
    };
}

static inline SpawnRequest SpawnRequest__overlay(ConstExecPayloadPtr payload)
{
    return (SpawnRequest){
        .mode = P_OVERLAY,
        .requested_user_id = SPAWN_INHERIT_USER_ID,
        .auth_ticket_id = SPAWN_INHERIT_AUTH_TICKET,
        .stdio = SPAWN_STDIO_DEFAULT,
        .arguments = EmptyArray(SpawnArguments),
        .overlay_payload = (uint64_t)payload,
        .overlay_payload_size = ExecPayload__size(payload),
    };
}

#endif
