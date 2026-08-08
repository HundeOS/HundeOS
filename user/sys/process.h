#ifndef __USER_SYS_PROCESS_H
#define __USER_SYS_PROCESS_H

#include <stddef.h>

#include "syscalls/process.h"

ProcessListResult process_list(ProcessInfoPtr entries, size_t max_entries);
ProcessSignalResult kill_process(uint64_t pid, ProcessSignal signal);
ProcessChildEventResult wait_for_child_event(ProcessChildEventPtr event);

#endif
