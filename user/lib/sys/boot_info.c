#include "boot_info.h"
#include "service.h"
#include "syscall.h"

Result(BootHardwareInfo) BootHardwareInfo__load(void)
{
    BootHardwareInfo info = BootHardwareInfo__new();
    ServiceCode status = syscall(SERVICE_CALL, SERVICE_BOOT_HARDWARE_INFO, any(&info), 0, 0, 0);
    if (!service_ok(status))
    {
        return Error(BootHardwareInfo, bstr("Boot hardware information unavailable"), status);
    }
    return Success(BootHardwareInfo, info);
}
