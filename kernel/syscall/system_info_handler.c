#include "system_info_handler.h"

#include "syscalls/process.h"
#include "syscalls/system_info.h"
#include "../trace_buffer.h"

static BString invalid_system_info_request = bstr("Invalid system information request");
static BString system_info_permission_denied = bstr("System information permission denied");
static BString system_info_unavailable = bstr("System information unavailable");

Result(SystemCallResult) SystemInfo__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                             Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    SystemInfoOperation operation = (SystemInfoOperation)arg1;
    if (operation != SYSTEM_INFO_READ_SETTINGS && operation != SYSTEM_INFO_READ_KERNEL_TRACE)
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

            size_t size = operation == SYSTEM_INFO_READ_SETTINGS ? sizeof(SystemSettings) : (size_t)arg3;
            if (arg2 == 0 || size == 0 || size > KERNEL_TRACE_CAPACITY)
            {
                return Error(SystemCallResult, invalid_system_info_request, SYSTEM_INFO_INVALID_ARGUMENT);
            }
            match_optional(
                Process__writable_user_kernel_mapping(process, (Address)arg2, size),
                some(target)
                {
                    if (operation == SYSTEM_INFO_READ_SETTINGS)
                    {
                        match_optional(
                            kernel->system_info,
                            some(info)
                            {
                                *(SystemSettingsPtr)target.virtual_addr =
                                    SystemSettings__new(info.io_latency_tracing_enabled);
                                return Success(SystemCallResult, SYSTEM_INFO_OK);
                            },
                            none_do_nothing()
                        );
                        return Error(SystemCallResult, system_info_unavailable, SYSTEM_INFO_UNAVAILABLE);
                    }
                    match_result(
                        KernelTrace__snapshot((uint8_t *)target.virtual_addr, size),
                        success(copied)
                        {
                            return Success(SystemCallResult, copied);
                        },
                        error(code, message)
                        {
                            return Error(SystemCallResult, message, code);
                        }
                    );
                },
                none_do_nothing()
            );
            return Error(SystemCallResult, invalid_system_info_request, SYSTEM_INFO_INVALID_ARGUMENT);
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, system_info_permission_denied, SYSTEM_INFO_PERMISSION_DENIED);
}
