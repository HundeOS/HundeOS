#ifndef __KERNEL_SYSCALL_HANDLER_H
#define __KERNEL_SYSCALL_HANDLER_H
#include "types/types.h"
#include <stdint.h>

typedef enum SystemCall
{
    PRINT = 0x21,
    READ = 0x22,
    WRITE = 0x23,
    EXIT = 0x00,
    PING = 0xFF,
};

int SyscallHandler__handle(int syscall_id, Anything arg1, Anything arg2, Anything arg3);

#endif