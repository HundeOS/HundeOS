#include "thread_wait.h"
#include "syscall.h"

ThreadWaitStatus thread_wait(ThreadWaitWordPtr word, ThreadWaitValue expected, ThreadWaitTimeout timeout_usec)
{
    return (ThreadWaitStatus)syscall(SYSCALL_THREAD_WAIT, (Anything)word, expected, timeout_usec, 0, 0);
}

ThreadWakeResult thread_wake(ThreadWaitWordPtr word, uint64_t count)
{
    return (ThreadWakeResult)syscall(SYSCALL_THREAD_WAKE, (Anything)word, count, 0, 0, 0);
}
