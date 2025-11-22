#ifndef __SYSCALL_SPAWN_H
#define __SYSCALL_SPAWN_H

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
    SPAWN_FAILED = -128,
}
SpawnErrors;

typedef int SpawnResult;
typedef int SpawnMode;


#endif