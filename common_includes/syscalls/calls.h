#ifndef _COMMON_INCLUDES_SYSCALLS_CALLS_H
#define _COMMON_INCLUDES_SYSCALLS_CALLS_H

#include "types.h"

typedef enum SystemCall
{
    PRINT = 0x21,
    PROC_EXIT = 0x00,
    IO_CALL = 0x80, // Retired file-I/O syscall number; do not reuse.
    SYSCALL_EXEC = 0x81,
    MEM_CALL = 0x82,
    SERVICE_CALL = 0x83,
    SYSCALL_PROCESS_LIST = 0x84,
    SYSCALL_SYSLOG = 0x85,
    SYSCALL_PROCESS_SIGNAL = 0x86,
    SYSCALL_PROCESS_WAIT_CHILD_EVENT = 0x87,
    SYSCALL_SHUTDOWN = 0x88,
    SYSCALL_EXEC_REQUEST = 0x89,
    SYSCALL_AUTH = 0x8A,
    SYSCALL_PANIC = 0x8B,
    SYSCALL_SLEEP = 0x8C,
    SYSCALL_PROCESS_CREDENTIALS = 0x8D,
    SYSCALL_BOOT_SPAWN = 0x8E,
    SYSCALL_BOOT_COMPLETE = 0x8F,
    // 0x90 retired: executable loading now completes as a deferred exec syscall.
    PING = 0xFF,
} SystemCall;

typedef enum SystemCallResults
{
    SYSCALL_OK = 0,
    SYSCALL_FAILURE = -1,
    SYSCALL_IO_BLOCK = -2
} SystemCallResults;

#endif
