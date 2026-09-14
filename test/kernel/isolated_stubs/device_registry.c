#include "../../../kernel/service/device_registry.h"

Result(ServiceCode) KernelDeviceRegistry__publish(KernelDeviceRegistryPtr self, ConstDeviceRecordPtr record)
{
    return Success(ServiceCode, SERVICE_OK);
}
