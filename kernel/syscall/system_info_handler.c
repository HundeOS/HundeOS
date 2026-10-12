#include "system_info_handler.h"

#include "syscalls/process.h"
#include "syscalls/system_info.h"

static BString invalid_system_info_request = bstr("Invalid system information request");
static BString system_info_permission_denied = bstr("System information permission denied");
static BString system_info_unavailable = bstr("System information unavailable");

Result(SystemCallResult) SystemInfo__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                             Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_system_info_request, SYSTEM_INFO_INVALID_ARGUMENT);
    }

    match_optional(
        *proc,
        some(process)
        {
            if (process->user_id != PROCESS_USER_HUND)
            {
                return Error(SystemCallResult, system_info_permission_denied, SYSTEM_INFO_PERMISSION_DENIED);
            }

            match_optional(
                kernel->system_info,
                some(info)
                {
                    SystemSettingsPtr target = (SystemSettingsPtr)arg1;
                    *target = SystemSettings__new(info.io_latency_tracing_enabled);
                    return Success(SystemCallResult, SYSTEM_INFO_OK);
                },
                none_do_nothing()
            );
            return Error(SystemCallResult, system_info_unavailable, SYSTEM_INFO_UNAVAILABLE);
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, system_info_permission_denied, SYSTEM_INFO_PERMISSION_DENIED);
}
