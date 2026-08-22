#include "system.h"

#include "syscall.h"

SystemCallResult shutdown_system(void)
{
    return syscall(SYSCALL_SHUTDOWN, 0, 0, 0, 0, 0);
}

SystemCallResult panic_system(void)
{
    return syscall(SYSCALL_PANIC, 0, 0, 0, 0, 0);
}
