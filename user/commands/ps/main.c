#include "../../user.h"
#include "sys/process.h"
#include "sys/mem.h"
#include "process/process_registry.h"

#define PS_INITIAL_CAPACITY 32
#define PS_PID_WIDTH 5
#define PS_CPU_WIDTH 3
#define PS_STATE_WIDTH 28
#define PS_WAIT_WIDTH 24
#define PS_CLASS_WIDTH 8
#define PS_USER_WIDTH 8
#define PS_CPU_TIME_WIDTH 12
#define PS_USER_WAIT_WIDTH 13
#define PS_CPU_PERCENT_WIDTH 12

#ifndef PS_ENTRYPOINT
#define PS_ENTRYPOINT main
#endif

int PS_ENTRYPOINT(void)
{
    size_t capacity = PS_INITIAL_CAPACITY;
    ProcessInfoPtr processes;
    match_result(
        mem_alloc_bytes(capacity * sizeof(ProcessInfo)),
        success(address)
        {
            processes = (ProcessInfoPtr)address;
        },
        error(code, message)
        {
            IO__printf(&bstr("ps: out of memory\n"));
            return 1;
        }
    );
    ProcessListResult count = process_list(processes, capacity);
    while (count == (ProcessListResult)capacity)
    {
        free(processes);
        // One extra slot distinguishes a full registry from a truncated read.
        if (capacity >= PROCESS_REGISTRY_CAPACITY + 1)
        {
            IO__printf(&bstr("ps: invalid process count\n"));
            return 1;
        }
        capacity *= 2;
        if (capacity > PROCESS_REGISTRY_CAPACITY + 1)
        {
            capacity = PROCESS_REGISTRY_CAPACITY + 1;
        }
        match_result(
            mem_alloc_bytes(capacity * sizeof(ProcessInfo)),
            success(address)
            {
                processes = (ProcessInfoPtr)address;
            },
            error(code, message)
            {
                IO__printf(&bstr("ps: out of memory\n"));
                return 1;
            }
        );
        count = process_list(processes, capacity);
    }
    if (count < 0)
    {
        free(processes);
        IO__printf(&bstr("ps: error %i\n"), (int)count);
        return 1;
    }

    IO__printf(&bstr("%-*s %-*s %-*s %-*s %-*s %-*s %*s %*s %*s %*s %s\n"), PS_PID_WIDTH, "PID", PS_CPU_WIDTH, "CPU",
               PS_STATE_WIDTH, "STATE", PS_WAIT_WIDTH, "WAIT", PS_CLASS_WIDTH, "CLASS", PS_USER_WIDTH, "USER",
               PS_CPU_TIME_WIDTH, "CPU TIME(s)", PS_USER_WAIT_WIDTH, "USER SLEEP(s)", PS_CPU_PERCENT_WIDTH, "CPU% (1s)",
               PS_CPU_PERCENT_WIDTH, "CPU% (100ms)", "NAME");
    for (ProcessListResult i = 0; i < count; i++)
    {
        uint64_t usec = processes[i].cpu_usage.total_usec;
        uint64_t long_usage = processes[i].cpu_usage.long_basis_points;
        uint64_t short_usage = processes[i].cpu_usage.short_basis_points;
        BString cpu_time = BString__sprintf(&bstr("%llu.%03u"), (unsigned long long)(usec / 1000000),
                                            (unsigned int)((usec / 1000) % 1000));
        BString user_wait =
            BString__sprintf(&bstr("%llu.%03u"), (unsigned long long)(processes[i].user_wait_usec / 1000000),
                             (unsigned int)((processes[i].user_wait_usec / 1000) % 1000));
        BString long_percent =
            BString__sprintf(&bstr("%u.%02u"), (unsigned int)(long_usage / 100), (unsigned int)(long_usage % 100));
        BString short_percent =
            BString__sprintf(&bstr("%u.%02u"), (unsigned int)(short_usage / 100), (unsigned int)(short_usage % 100));
        IO__printf(&bstr("%-*i %-*u %-*.*s %-*.*s %-*.*s %-*.*s %*B %*B %*B %*B %s\n"), PS_PID_WIDTH,
                   (int)processes[i].pid, PS_CPU_WIDTH, (unsigned int)processes[i].cpu, PS_STATE_WIDTH, PS_STATE_WIDTH,
                   processes[i].state, PS_WAIT_WIDTH, PS_WAIT_WIDTH, processes[i].wait_reason, PS_CLASS_WIDTH,
                   PS_CLASS_WIDTH, processes[i].class_name, PS_USER_WIDTH, PS_USER_WIDTH, processes[i].user_name,
                   PS_CPU_TIME_WIDTH, &cpu_time, PS_USER_WAIT_WIDTH, &user_wait, PS_CPU_PERCENT_WIDTH, &long_percent,
                   PS_CPU_PERCENT_WIDTH, &short_percent, processes[i].name);
    }

    free(processes);
    return 0;
}
