#ifndef __KERNEL_SYSCALL_EXEC_H
#define __KERNEL_SYSCALL_EXEC_H

#include "syscall_dispatcher.h"

Result(SystemCallResult) Exec__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                       Anything arg3, Anything arg4, Anything arg5);
Result(SystemCallResult) ExecRequest__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                              Anything arg2, Anything arg3, Anything arg4, Anything arg5);

Result(SystemCallResult) BootSpawn__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                            Anything arg2, Anything arg3, Anything arg4, Anything arg5);
Result(SystemCallResult) BootComplete__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                               Anything arg2, Anything arg3, Anything arg4, Anything arg5);
#endif
