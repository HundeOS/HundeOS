#ifndef __KERNEL_SYSCALL_PROCESS_HANDLER_H
#define __KERNEL_SYSCALL_PROCESS_HANDLER_H

#include "../kernel_core.h"
#include "syscall.h"

Result(SystemCallResult) ProcessCredentials__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                                     Anything arg2, Anything arg3, Anything arg4, Anything arg5);

Result(SystemCallResult) ProcessList__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                              Anything arg2, Anything arg3, Anything arg4, Anything arg5);
Result(SystemCallResult) ProcessWaitChildEvent__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                                        Anything arg2, Anything arg3, Anything arg4, Anything arg5);
Result(SystemCallResult) ProcessSleep__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                               Anything arg2, Anything arg3, Anything arg4, Anything arg5);
Result(SystemCallResult) ProcessTimes__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                               Anything arg2, Anything arg3, Anything arg4, Anything arg5);
Result(SystemCallResult) ProcessIdentity__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                                  Anything arg2, Anything arg3, Anything arg4, Anything arg5);
Result(SystemCallResult) ProcessFork__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                              Anything arg2, Anything arg3, Anything arg4, Anything arg5);
Result(SystemCallResult) ProcessGroup__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                               Anything arg2, Anything arg3, Anything arg4, Anything arg5);

#endif
