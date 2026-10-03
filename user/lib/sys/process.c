#include "process.h"

#include "syscall.h"
#include "syscalls/calls.h"

static uint64_t current_pid;

Result(ProcessCredentials) process_credentials(uint64_t pid)
{
    ProcessCredentials credentials = ProcessCredentials__new(0, 0);
    int64_t status = (int64_t)syscall(SYSCALL_PROCESS_CREDENTIALS, pid, (Anything)&credentials, 0, 0, 0);
    if (status != PROCESS_CREDENTIALS_OK)
    {
        return Error(ProcessCredentials, bstr("Cannot resolve process credentials"), status);
    }
    return Success(ProcessCredentials, credentials);
}

Result(ProcessIdentity) process_identity(void)
{
    ProcessIdentity identity = ProcessIdentity__new(0, 0);
    int64_t status = (int64_t)syscall(SYSCALL_PROCESS_IDENTITY, (Anything)&identity, 0, 0, 0, 0);
    if (status != PROCESS_IDENTITY_OK)
    {
        return Error(ProcessIdentity, bstr("Cannot resolve process identity"), status);
    }
    return Success(ProcessIdentity, identity);
}

ProcessForkResult process_fork(void)
{
    return (ProcessForkResult)syscall(SYSCALL_PROCESS_FORK, PROCESS_FORK_PREPARE, 0, 0, 0, 0);
}

ProcessForkResult process_fork_resume(uint64_t child_pid)
{
    return (ProcessForkResult)syscall(SYSCALL_PROCESS_FORK, PROCESS_FORK_RESUME, child_pid, 0, 0, 0);
}

ProcessForkResult process_fork_abort(uint64_t child_pid)
{
    return (ProcessForkResult)syscall(SYSCALL_PROCESS_FORK, PROCESS_FORK_ABORT, child_pid, 0, 0, 0);
}

void process_set_startup_pid(uint64_t pid)
{
    current_pid = pid;
}

uint64_t process_current_pid(void)
{
    return current_pid;
}

ProcessListResult process_list(ProcessInfoPtr entries, size_t max_entries)
{
    ProcessListRequest request = ProcessListRequest__new(entries, max_entries);
    return (ProcessListResult)syscall(SYSCALL_PROCESS_LIST, (Anything)&request, 0, 0, 0, 0);
}

ProcessSignalResult kill_process(int64_t target, ProcessSignal signal)
{
    return (ProcessSignalResult)syscall(SYSCALL_PROCESS_SIGNAL, (Anything)target, (Anything)signal, 0, 0, 0);
}

ProcessSignalResult process_signal_control(ProcessSignalControlRequestPtr request)
{
    return (ProcessSignalResult)syscall(SYSCALL_PROCESS_SIGNAL_CONTROL, (Anything)request, 0, 0, 0, 0);
}

ProcessChildEventResult wait_for_child_event(ProcessChildEventPtr event)
{
    return wait_for_child_event_for(-1, 0, event);
}

ProcessChildEventResult wait_for_child_event_for(int64_t child_selection, uint64_t options, ProcessChildEventPtr event)
{
    return (ProcessChildEventResult)syscall(SYSCALL_PROCESS_WAIT_CHILD_EVENT, (Anything)event,
                                            (Anything)child_selection, (Anything)options, 0, 0);
}

ProcessTimesResult process_times(ProcessTimesPtr times)
{
    return (ProcessTimesResult)syscall(SYSCALL_PROCESS_TIMES, (Anything)times, 0, 0, 0, 0);
}

ProcessGroupResult process_group_control(ProcessGroupRequestPtr request)
{
    return (ProcessGroupResult)syscall(SYSCALL_PROCESS_GROUP, (Anything)request, 0, 0, 0, 0);
}
