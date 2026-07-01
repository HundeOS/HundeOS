#include "init.h"
#include "../mutex.h"
#include "syscall_dispatcher.h"
#include "../types/result.h"
#include "print_handler.h"
#include "ping_handler.h"
#include "exit_handler.h"
#include "exec_handler.h"
#include "io_handler.h"
#include "mem_handler.h"
#include "service_handler.h"
#include "process_handler.h"
#include "syslog_handler.h"
#include "../debug.h"
#include "../print.h"

#define REGISTER(self, syscall, handler) \
    __d_printf(DEBUG, "Init__syscalls() - %p", syscall); \
    ResultMatch(RegisterResult, SyscallDispatcher__register(self, syscall, handler);); \
        IsError(code, message, \
            {  \
                mutex_unlock(&self->init_lock); \
                __d_println(DEBUG, "Init__syscalls() - error"); \
                return Error(RegisterResult, message, code); \
            }); \
        IsSuccess(_, __d_printf(DEBUG, "Init__syscalls() - %p - reg success", syscall);)\
    End \


Result(RegisterResult) Init__syscalls(SyscallDispatcherPtr self)
{
    __d_println(SYSCALL, "Init__syscalls()");
    CriticalSection(self->init_lock,
    {
        __d_println(DEBUG, "Init__syscalls() - doing");
        if (!self->init_done)
        {
            REGISTER(self, PRINT, &Print__handler);
            REGISTER(self, PING, &Ping__handler);
            REGISTER(self, PROC_EXIT, &Exit__handler);
            REGISTER(self, IO_CALL, &IO__handler);
            REGISTER(self, SYSCALL_EXEC, &Exec__handler);
            REGISTER(self, MEM_CALL, &Mem__handler);
            REGISTER(self, SERVICE_CALL, &Service__handler);
            REGISTER(self, SYSCALL_PROCESS_LIST, &ProcessList__handler);
            REGISTER(self, SYSCALL_SYSLOG, &Syslog__handler);
            self->init_done = true;
            __d_println(SYSCALL, "Init__syscalls() - done ok");
        }
    });
    return Success(RegisterResult, REGISTER_OK);
}
