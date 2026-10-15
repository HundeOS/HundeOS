#ifndef __USER_SYS_PROCESS_H
#define __USER_SYS_PROCESS_H

#include <stddef.h>

#include "syscalls/process.h"
#include "process/process_registry.h"

ProcessListResult process_list(ProcessInfoPtr entries, size_t max_entries);
Result(ProcessCredentials) process_credentials(uint64_t pid);
Result(ProcessIdentity) process_identity(void);
ProcessForkResult process_fork(void);
ProcessForkResult process_fork_resume(uint64_t child_pid);
ProcessForkResult process_fork_abort(uint64_t child_pid);
void process_set_startup_pid(uint64_t pid);
uint64_t process_current_pid(void);
ProcessSignalResult kill_process(int64_t target, ProcessSignal signal);
ProcessSignalResult process_signal_control(ProcessSignalControlRequestPtr request);
void process_signal_restorer(void) __attribute__((noreturn));
ProcessChildEventResult wait_for_child_event(ProcessChildEventPtr event);
ProcessChildEventResult wait_for_child_event_for(int64_t child_selection, uint64_t options, ProcessChildEventPtr event);
ProcessTimesResult process_times(ProcessTimesPtr times);
ProcessGroupResult process_group_control(ProcessGroupRequestPtr request);
Result(ConstProcessRegistryPtr) process_registry_map(void);

#endif
