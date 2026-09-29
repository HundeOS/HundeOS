#include "process_metrics.h"

#include "ipc/ipc_platform.h"
#include "mem.h"
#include "process.h"

#define PROCESS_METRICS_TIMEOUT_USEC 5000000ULL
#define PROCESS_METRICS_INITIAL_CAPACITY 32ULL

Result(ProcessInfo) ProcessMetrics__info(uint64_t pid)
{
    uint64_t capacity = PROCESS_METRICS_INITIAL_CAPACITY;
    while (capacity <= PROCESS_REGISTRY_CAPACITY + 1)
    {
        ProcessInfoPtr entries;
        match_result(
            mem_alloc_bytes(capacity * sizeof(ProcessInfo)),
            success(address)
            {
                entries = (ProcessInfoPtr)address;
            },
            error(code, message)
            {
                return Error(ProcessInfo, message, code);
            }
        );

        ProcessListResult count = process_list(entries, capacity);
        if (count < 0)
        {
            free(entries);
            return Error(ProcessInfo, bstr("Cannot snapshot processes"), count);
        }
        for (ProcessListResult index = 0; index < count; index++)
        {
            if (entries[index].pid == pid)
            {
                ProcessInfo found = entries[index];
                free(entries);
                return Success(ProcessInfo, found);
            }
        }

        free(entries);
        if ((uint64_t)count < capacity)
        {
            return Error(ProcessInfo, bstr("Process missing from snapshot"), -1);
        }
        if (capacity == PROCESS_REGISTRY_CAPACITY + 1)
        {
            return Error(ProcessInfo, bstr("Process snapshot exceeds registry capacity"), -1);
        }
        capacity *= 2;
        if (capacity > PROCESS_REGISTRY_CAPACITY + 1)
        {
            capacity = PROCESS_REGISTRY_CAPACITY + 1;
        }
    }
    return Error(ProcessInfo, bstr("Invalid process snapshot capacity"), -1);
}

Result(ProcessRecord)
    ProcessMetrics__snapshot_after(ConstProcessRegistryPtr registry, uint64_t pid, uint64_t sampled_after_usec)
{
    uint64_t start = IpcPlatform__monotonic_usec();
    while (IpcPlatform__monotonic_usec() - start < PROCESS_METRICS_TIMEOUT_USEC)
    {
        match_result(
            ProcessRegistry__find(registry, pid),
            success(found)
            {
                match_optional(
                    found,
                    some(record)
                    {
                        if (record.sampled_at_usec >= sampled_after_usec)
                        {
                            return Success(ProcessRecord, record);
                        }
                    },
                    none()
                    {
                        return Error(ProcessRecord, bstr("Process missing from registry"), -1);
                    }
                );
            },
            error(code, message)
            {
                if (code != ATOMIC_RECORD_BUSY)
                {
                    return Error(ProcessRecord, message, code);
                }
            }
        );
        IpcPlatform__relax();
    }
    return Error(ProcessRecord, bstr("No fresh process sample within five seconds"), -1);
}
