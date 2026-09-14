#ifndef HUND_KERNEL_PROCESS_REGISTRY_H
#define HUND_KERNEL_PROCESS_REGISTRY_H
#include "../../common/process/process_registry.h"
#include "service_directory.h"

typedef struct KernelProcessRegistry
{
    mutex_t lock;
    UserAddress mapping;
} KernelProcessRegistry;
typedef KernelProcessRegistry * KernelProcessRegistryPtr;
static inline KernelProcessRegistry KernelProcessRegistry__new(void)
{
    return (KernelProcessRegistry){.lock = MUTEX_INIT, .mapping = NULL_USER_ADDRESS};
}
Result(ServiceCode) KernelProcessRegistry__map_process(KernelProcessRegistryPtr self, ProcessPtr process);
void KernelProcessRegistry__set(KernelProcessRegistryPtr self, uint64_t cpu, uint64_t index,
                                ConstProcessRecordPtr record);
void KernelProcessRegistry__set_count(KernelProcessRegistryPtr self, uint64_t cpu, uint64_t count);
ProcessRecord ProcessRecord__new(ProcessPtr process, uint64_t cpu);
#endif
