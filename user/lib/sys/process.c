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

ProcessSignalResult kill_process(uint64_t pid, ProcessSignal signal)
{
    return (ProcessSignalResult)syscall(SYSCALL_PROCESS_SIGNAL, (Anything)pid, (Anything)signal, 0, 0, 0);
}

ProcessChildEventResult wait_for_child_event(ProcessChildEventPtr event)
{
    return (ProcessChildEventResult)syscall(SYSCALL_PROCESS_WAIT_CHILD_EVENT, (Anything)event, 0, 0, 0, 0);
}
