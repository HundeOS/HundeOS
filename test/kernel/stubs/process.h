#ifndef __KERNEL_STUBS_PROCESS_H
#define __KERNEL_STUBS_PROCESS_H

#include "../../../kernel/process.h"

void current_process_reset();
bool context_switched_with_scheduler_lock(void);
typedef struct TestStartupStdio
{
    uint64_t pid;
    int64_t input;
    int64_t output;
    int64_t error;
} TestStartupStdio;
TestStartupStdio startup_stdio(void);
ProcessPtr current_running_process();
ProcessPtr active_address_space_process();

Result(Anything) deferred_syscall_result();
void clear_deferred_syscall_result();

#endif
