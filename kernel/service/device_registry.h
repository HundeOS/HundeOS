#ifndef HUND_KERNEL_DEVICE_REGISTRY_H
#define HUND_KERNEL_DEVICE_REGISTRY_H
#include "../../common/devices/device_registry.h"
#include "service_directory.h"

typedef struct KernelDeviceRegistry
{
    mutex_t lock;
    UserAddress mapping;
} KernelDeviceRegistry;

typedef KernelDeviceRegistry * KernelDeviceRegistryPtr;

static inline KernelDeviceRegistry KernelDeviceRegistry__new(void)
{
    return (KernelDeviceRegistry){.lock = MUTEX_INIT, .mapping = NULL_USER_ADDRESS};
}

Result(ServiceCode) KernelDeviceRegistry__init(KernelDeviceRegistryPtr self);
Result(ServiceCode) KernelDeviceRegistry__map_process(KernelDeviceRegistryPtr self, ProcessPtr process);
Result(ServiceCode) KernelDeviceRegistry__publish(KernelDeviceRegistryPtr self, ConstDeviceRecordPtr record);
void KernelDeviceRegistry__remove_process(KernelDeviceRegistryPtr self, uint64_t pid);
#endif
