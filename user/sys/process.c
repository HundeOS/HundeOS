#include "process.h"

#include "syscall.h"
#include "syscalls/calls.h"

ProcessListResult process_list(ProcessInfoPtr entries, size_t max_entries)
{
    ProcessListRequest request = (ProcessListRequest) {
        .entries = entries,
        .max_entries = max_entries,
    };
    return (ProcessListResult) syscall(SYSCALL_PROCESS_LIST, (Anything) &request, 0, 0, 0, 0);
}
