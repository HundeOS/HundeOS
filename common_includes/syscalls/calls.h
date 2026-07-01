#ifndef _COMMON_INCLUDES_SYSCALLS_CALLS_H
#define _COMMON_INCLUDES_SYSCALLS_CALLS_H
#include <stdint.h>

typedef enum SystemCall
{
    PRINT = 0x21,
    PROC_EXIT = 0x00,
    IO_CALL = 0x80,
    SYSCALL_EXEC = 0x81,
    MEM_CALL = 0x82,
    SERVICE_CALL = 0x83,
    SYSCALL_PROCESS_LIST = 0x84,
    SYSCALL_SYSLOG = 0x85,
    PING = 0xFF,
} SystemCall;

typedef enum SystemCallResults
{
    SYSCALL_OK = 0,
    SYSCALL_FAILURE = -1,
    SYSCALL_IO_BLOCK = -2
}
SystemCallResults;


typedef uint64_t SystemCallResult;
typedef uint64_t Anything;

#endif
