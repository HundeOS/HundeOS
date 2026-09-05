#include "process_sleep.h"

#include "syscall.h"

ProcessSleepResult sleep_usec(uint64_t usec)
{
    return (ProcessSleepResult) syscall(SYSCALL_SLEEP, (Anything) usec, 0, 0, 0, 0);
}
