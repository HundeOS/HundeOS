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
#include "signal_handler.h"
#include "shutdown_handler.h"
#include "auth.h"
#include "panic_handler.h"
#include "../debug.h"
#include "../print.h"

#define REGISTER(self, syscall, handler)                                                                               \
    __d_printf(DEBUG, "Init__syscalls() - %p", syscall); \
    match_result(SyscallDispatcher__register(self, syscall, handler), /* hundelint: ignore-match-branch-macros */                                                             \
        success(_) { __d_printf(DEBUG, "Init__syscalls() - %p - reg success", syscall); },                             \
        error(code, message) {                                                                                         \
            mutex_unlock(&self->init_lock);                                                                            \
            __d_println(DEBUG, "Init__syscalls() - error");                                                            \
            return Error(RegisterResult, message, code);                                                               \
        });

Result(RegisterResult) Init__syscalls(SyscallDispatcherPtr self)
{
    __d_println(SYSCALL, "Init__syscalls()");
    critical_section(self->init_lock, {
        __d_println(DEBUG, "Init__syscalls() - doing");
        if (!self->init_done)
        {
            REGISTER(self, PRINT, &Print__handler);
            REGISTER(self, PING, &Ping__handler);
            REGISTER(self, PROC_EXIT, &Exit__handler);
            REGISTER(self, IO_CALL, &IO__handler);
            REGISTER(self, SYSCALL_EXEC, &Exec__handler);
            REGISTER(self, SYSCALL_EXEC_REQUEST, &ExecRequest__handler);
            REGISTER(self, MEM_CALL, &Mem__handler);
            REGISTER(self, SERVICE_CALL, &Service__handler);
            REGISTER(self, SYSCALL_PROCESS_LIST, &ProcessList__handler);
            REGISTER(self, SYSCALL_PROCESS_CREDENTIALS, &ProcessCredentials__handler);
            REGISTER(self, SYSCALL_PROCESS_WAIT_CHILD_EVENT, &ProcessWaitChildEvent__handler);
            REGISTER(self, SYSCALL_SYSLOG, &Syslog__handler);
            REGISTER(self, SYSCALL_PROCESS_SIGNAL, &Signal__handler);
            REGISTER(self, SYSCALL_SHUTDOWN, &Shutdown__handler);
            REGISTER(self, SYSCALL_AUTH, &Auth__handler);
            REGISTER(self, SYSCALL_PANIC, &Panic__handler);
            REGISTER(self, SYSCALL_SLEEP, &ProcessSleep__handler);
            self->init_done = true;
            __d_println(SYSCALL, "Init__syscalls() - done ok");
        }
    });
    return Success(RegisterResult, REGISTER_OK);
}
