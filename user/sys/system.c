#include "system.h"

#include "syscall.h"

SystemCallResult shutdown_system(void)
{
    return syscall(SYSCALL_SHUTDOWN, 0, 0, 0, 0, 0);
}
