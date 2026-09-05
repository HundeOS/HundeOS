#ifndef __USER_SYS_SYSCALL_H
#define __USER_SYS_SYSCALL_H

#include "syscalls/calls.h"

#define any(a) ( (Anything) (a))

SystemCallResult syscall(SystemCall num, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5);


#endif