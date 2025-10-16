#include "init.h"
#include "../mutex.h"
#include "syscall_handler.h"
#include "../types/result.h"
#include "print_handler.h"
#include "ping_handler.h"
#include "exit_handler.h"

static mutex_t init_lock = MUTEX_INIT;
static bool init_done = false;

#define REGISTER(syscall, handler) \
    ResultMatch(RegisterResult, SyscallHandler__register(syscall, handler);); \
        IsError(code, message, \
            {  \
                mutex_unlock(&init_lock); \
                return Error(RegisterResult, message, code); \
            }); \
    End \


Result(RegisterResult) Init__syscalls()
{
    mutex_lock(&init_lock);
    if (!init_done)
    {
        REGISTER(PRINT, &Print__handler);
        REGISTER(PING, &Ping__handler);
        REGISTER(PROC_EXIT, &Exit__handler);
        init_done = true;
    }
    mutex_unlock(&init_lock);
    return Success(RegisterResult, REGISTER_OK);
}