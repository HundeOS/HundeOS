#include <errno.h>

#include "include/sys/wait.h"

#include "sys/process.h"
#include "syscalls/calls.h"

pid_t waitpid(pid_t pid, int * status, int options)
{
    int supported_options = WNOHANG | WUNTRACED | WCONTINUED;
    if ((options & ~supported_options) != 0)
    {
        errno = EINVAL;
        return -1;
    }

    ProcessChildEvent event = ProcessChildEvent__new();
    ProcessChildEventResult result = wait_for_child_event_for((int64_t)pid, (uint64_t)options, &event);
    if ((int64_t)result == SYSCALL_INTERRUPTED)
    {
        errno = EINTR;
        return -1;
    }
    if (result == PROCESS_CHILD_EVENT_PENDING)
    {
        return 0;
    }
    if (result != PROCESS_CHILD_EVENT_OK)
    {
        errno = result == PROCESS_CHILD_EVENT_NO_CHILD ? ECHILD : EINVAL;
        return -1;
    }

    if (status != NULL)
    {
        *status = (int)event.status;
    }
    return (pid_t)event.pid;
}

pid_t wait(int * status)
{
    return waitpid(-1, status, 0);
}
