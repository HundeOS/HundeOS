#include "../../user.h"
#include "sys/process.h"

#define PS_MAX_PROCESSES 32
#define PS_PID_WIDTH 5
#define PS_STATE_WIDTH 28
#define PS_WAIT_WIDTH 24
#define PS_CLASS_WIDTH 8
#define PS_USER_WIDTH 8

#ifndef PS_ENTRYPOINT
#define PS_ENTRYPOINT main
#endif

int PS_ENTRYPOINT(void)
{
    ProcessInfo processes[PS_MAX_PROCESSES];
    ProcessListResult count = process_list(processes, PS_MAX_PROCESSES);
    if (count < 0)
    {
        IO__printf(&bstr("ps: error %i\n"), (int)count);
        return 1;
    }

    IO__printf(&bstr("%-*s %-*s %-*s %-*s %-*s %s\n"), PS_PID_WIDTH, "PID", PS_STATE_WIDTH, "STATE", PS_WAIT_WIDTH,
               "WAIT", PS_CLASS_WIDTH, "CLASS", PS_USER_WIDTH, "USER", "NAME");
    for (ProcessListResult i = 0; i < count; i++)
    {
        IO__printf(&bstr("%-*i %-*.*s %-*.*s %-*.*s %-*.*s %s\n"), PS_PID_WIDTH, (int)processes[i].pid, PS_STATE_WIDTH,
                   PS_STATE_WIDTH, processes[i].state, PS_WAIT_WIDTH, PS_WAIT_WIDTH, processes[i].wait_reason,
                   PS_CLASS_WIDTH, PS_CLASS_WIDTH, processes[i].class_name, PS_USER_WIDTH, PS_USER_WIDTH,
                   processes[i].user_name, processes[i].name);
    }

    return 0;
}
