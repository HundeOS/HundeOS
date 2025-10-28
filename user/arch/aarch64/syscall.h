#ifndef __USER_SYSCALL_H
#define __USER_SYSCALL_H
#include <stdint.h>
#include <stdint.h>

typedef enum SystemCall
{
    PRINT = 0x21,
    PROC_EXIT = 0x00,
    IO_CALL = 0x80,
    PING = 0xFF,
} SystemCall;

typedef uint64_t SystemCallResult;

typedef uint64_t Anything;

#define any(a) ( (Anything) (a))

SystemCallResult syscall(SystemCall num, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5);


#endif