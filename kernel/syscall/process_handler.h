#ifndef __KERNEL_SYSCALL_PROCESS_HANDLER_H
#define __KERNEL_SYSCALL_PROCESS_HANDLER_H

#include "../kernel_core.h"
#include "syscall.h"

Result(SystemCallResult)
    ThreadWait__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                        Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);
Result(SystemCallResult)
    ThreadWake__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                        Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);

Result(SystemCallResult)
    ThreadLocalImage__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                              Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);

Result(SystemCallResult)
    ThreadCreate__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                          Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);
Result(SystemCallResult)
    ThreadExit__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                        Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);
Result(SystemCallResult)
    ThreadJoin__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                        Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);

Result(SystemCallResult)
    ProcessCredentials__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);

Result(SystemCallResult)
    ProcessList__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                         Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);
Result(SystemCallResult)
    ProcessWaitChildEvent__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                   Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);
Result(SystemCallResult)
    ProcessSleep__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                          Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);
Result(SystemCallResult)
    ProcessIdleWait__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                             Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);
Result(SystemCallResult)
    ProcessTimes__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                          Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);
Result(SystemCallResult)
    ProcessIdentity__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                             Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);
Result(SystemCallResult)
    ProcessFork__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                         Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);
Result(SystemCallResult)
    ProcessGroup__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                          Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);
Result(SystemCallResult)
    ProcessRegistryMap__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread);

#endif
