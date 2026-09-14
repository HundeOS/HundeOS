#ifndef __USER_SYS_PROCESS_H
#define __USER_SYS_PROCESS_H

#include <stddef.h>

#include "syscalls/process.h"

ProcessListResult process_list(ProcessInfoPtr entries, size_t max_entries);
Result(ProcessCredentials) process_credentials(uint64_t pid);
void process_set_startup_pid(uint64_t pid);
uint64_t process_current_pid(void);
ProcessSignalResult kill_process(uint64_t pid, ProcessSignal signal);
ProcessChildEventResult wait_for_child_event(ProcessChildEventPtr event);

#endif
