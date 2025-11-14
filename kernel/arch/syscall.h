#ifndef __KERNEL_ARCH_SYSCALL_H
#define __KERNEL_ARCH_SYSCALL_H
#include "../syscall/syscall.h"
#include "../process.h"

void SyscallResult__set(ProcessPtr proc, SystemCallResult res);

#endif