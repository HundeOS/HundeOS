#include "diagnostics.h"
#include "ipc/ipc_platform.h"

Result(ProcessRecord) WaitProbe__snapshot(ConstProcessRegistryPtr registry, uint64_t pid, uint64_t sampled_after)
{
    uint64_t start = IpcPlatform__monotonic_usec();
    while (IpcPlatform__monotonic_usec() - start < 5000000ULL)
    {
        match_result(
            ProcessRegistry__find(registry, pid),
            success(found)
            {
                match_optional(
                    found,
                    some(record)
                    {
                        if (record.sampled_at_usec >= sampled_after)
                        {
                            return Success(ProcessRecord, record);
                        }
                    },
                    none()
                    {
                        return Error(ProcessRecord, bstr("Probe process missing"), -1);
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
