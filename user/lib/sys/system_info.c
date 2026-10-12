#include "system_info.h"

#include "syscall.h"
#include "syscalls/calls.h"

Result(SystemSettings) SystemSettings__load(void)
{
    SystemSettings settings = SystemSettings__new(false);
    SystemInfoResult status = (SystemInfoResult)syscall(SYSCALL_SYSTEM_INFO, any(&settings), 0, 0, 0, 0);
    if (status != SYSTEM_INFO_OK)
    {
        return Error(SystemSettings, bstr("Cannot load system settings"), status);
    }
    return Success(SystemSettings, settings);
}
