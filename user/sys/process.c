#include "process.h"

#include "syscall.h"
#include "syscalls/calls.h"

ProcessListResult process_list(ProcessInfoPtr entries, size_t max_entries)
{
    ProcessListRequest request = ProcessListRequest__new(entries, max_entries);
    return (ProcessListResult) syscall(SYSCALL_PROCESS_LIST, (Anything) &request, 0, 0, 0, 0);
}

ProcessSignalResult kill_process(uint64_t pid, ProcessSignal signal)
{
    return (ProcessSignalResult) syscall(SYSCALL_PROCESS_SIGNAL, (Anything) pid, (Anything) signal, 0, 0, 0);
}

ProcessChildEventResult wait_for_child_event(ProcessChildEventPtr event)
{
    return (ProcessChildEventResult) syscall(SYSCALL_PROCESS_WAIT_CHILD_EVENT, (Anything) event, 0, 0, 0, 0);
}
