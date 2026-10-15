#include "system_info.h"

#include "syscall.h"
#include "syscalls/calls.h"

Result(SystemSettings) SystemSettings__load(void)
{
    SystemSettings settings = SystemSettings__new(false);
    SystemInfoResult status =
        (SystemInfoResult)syscall(SYSCALL_SYSTEM_INFO, SYSTEM_INFO_READ_SETTINGS, any(&settings), 0, 0, 0);
    if (status != SYSTEM_INFO_OK)
    {
        return Error(SystemSettings, bstr("Cannot load system settings"), status);
    }
    return Success(SystemSettings, settings);
}

Result(KernelTraceSize) KernelTrace__load(uint8_t * destination, size_t capacity)
{
    int64_t result =
        (int64_t)syscall(SYSCALL_SYSTEM_INFO, SYSTEM_INFO_READ_KERNEL_TRACE, any(destination), capacity, 0, 0);
    if (result < 0)
    {
        return Error(KernelTraceSize, bstr("Cannot load kernel trace"), result);
    }
    return Success(KernelTraceSize, (KernelTraceSize)result);
}
