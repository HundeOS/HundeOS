#include "../../user.h"
#include "../../sys/process.h"

#define PS_MAX_PROCESSES 32

#ifndef PS_ENTRYPOINT
#define PS_ENTRYPOINT main
#endif

int PS_ENTRYPOINT(void)
{
    ProcessInfo processes[PS_MAX_PROCESSES];
    ProcessListResult count = process_list(processes, PS_MAX_PROCESSES);
    if (count < 0)
    {
        printf("ps: error %i\n", (int) count);
        return 1;
    }

    printf("PID %s WAIT CLASS USER NAME\n", "STATE");
    for (ProcessListResult i = 0; i < count; i++)
    {
        printf(
            "%i %s %s %s %s %s\n",
            (int) processes[i].pid,
            processes[i].state,
            processes[i].wait_reason,
            processes[i].class_name,
            processes[i].user_name,
            processes[i].name
        );
    }

    return 0;
}
