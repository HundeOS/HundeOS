#ifndef __USER_SYSCALL_H
#define __USER_SYSCALL_H
#include <stdint.h>

typedef enum SystemCall {
    CONSOLE_PRINT = 0x21,
} SystemCall;

typedef uint64_t SystemCallResult;

SystemCallResult syscall(SystemCall num, uint64_t arg1, uint64_t arg2, uint64_t arg3);


#endif