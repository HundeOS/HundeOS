#include "device_registry.h"
#include "../arch/page_table.h"
#include "../kalloc.h"
#include "../process.h"

Result(ServiceCode) KernelDeviceRegistry__init(KernelDeviceRegistryPtr self)
{
    critical_section(
        self->lock,
        {
            if (self->mapping.valid)
            {
                cs_return(Success(ServiceCode, SERVICE_OK));
            }
            match_result(
                Kernel__try_alloc_user(DEVICE_REGISTRY_VADDR, DEVICE_REGISTRY_SIZE, true, false, false),
                success(mapping)
                {
                    Kernel__memset(mapping.kernel_addr.virtual_addr, 0, DEVICE_REGISTRY_SIZE);
                    k_v_ptr(mapping, DeviceRegistry)->version = DEVICE_REGISTRY_VERSION;
                    self->mapping = mapping;
                    cs_return(Success(ServiceCode, SERVICE_OK));
                },
                error(code, message)
                {
                    cs_return(Error(ServiceCode, message, SERVICE_MAP_FAILED));
                }
            );
        }
    );
}

Result(ServiceCode) KernelDeviceRegistry__map_process(KernelDeviceRegistryPtr self, ProcessPtr process)
{
    match_result(
        KernelDeviceRegistry__init(self),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ServiceCode, message, code);
        }
    );
    PageTable__map_shared(&process->page_table, process->kernel, self->mapping, DEVICE_REGISTRY_SIZE);
    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) KernelDeviceRegistry__publish(KernelDeviceRegistryPtr self, ConstDeviceRecordPtr record)
{
    match_result(
        KernelDeviceRegistry__init(self),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ServiceCode, message, code);
        }
    );
    if (!BString__starts_with(&record->path, &bstr("/dev/")))
    {
        return Error(ServiceCode, bstr("Invalid device publication"), SERVICE_INVALID_BUFFER);
    }
    critical_section(
        self->lock,
        {
            DeviceRegistryPtr directory = k_v_ptr(self->mapping, DeviceRegistry);
            uint64_t slot = DEVICE_REGISTRY_CAPACITY;
            for (uint64_t index = 0; index < DEVICE_REGISTRY_CAPACITY; index++)
            {
                match_result(
                    DeviceRegistry__at(directory, index),
                    success(existing)
                    {
                        if (existing.kind == DEVICE_ABSENT && slot == DEVICE_REGISTRY_CAPACITY)
                        {
                            slot = index;
                        }
                        if (existing.kind != DEVICE_ABSENT && BString__equals(&existing.path, &record->path))
                        {
                            cs_return(Error(ServiceCode, bstr("Device already published"),
                                            SERVICE_DEVICE_ALREADY_REGISTERED));
                        }
                    },
                    error(code, message)
                    {
                        cs_return(Error(ServiceCode, message, SERVICE_MAP_FAILED));
                    }
                );
            }
            DeviceRecord published = *record;
            if (published.kind == DEVICE_BLOCK && BString__is_empty(&published.alias))
            {
                published.alias = BString__sprintf(&bstr("/dev/hd%i"), (int)slot);
            }
            match_result(
                DeviceRegistry__set(directory, slot, &published),
                success(code)
                {
                    cs_return(Success(ServiceCode, SERVICE_OK));
                },
                error(code, message)
                {
                    cs_return(Error(ServiceCode, message, SERVICE_EVENT_QUEUE_FULL));
                }
            );
        }
    );
}

void KernelDeviceRegistry__remove_process(KernelDeviceRegistryPtr self, uint64_t pid)
{
    if (!self->mapping.valid || pid == 0)
    {
        return;
    }
    critical_section(
        self->lock,
        {
            DeviceRegistryPtr directory = k_v_ptr(self->mapping, DeviceRegistry);
            for (uint64_t index = 0; index < DEVICE_REGISTRY_CAPACITY; index++)
            {
                match_result(
                    DeviceRegistry__at(directory, index),
                    success(record)
                    {
                        if (record.kind != DEVICE_ABSENT && record.owner_pid == pid)
                        {
                            DeviceRecord empty =
                                DeviceRecord__new(DEVICE_ABSENT, 0, &empty_bstr(), &empty_bstr(), 0, 0);
                            DeviceRegistry__set(directory, index, &empty);
                        }
                    },
                    error(code, message)
                    {
                        cs_return_void();
                    }
                );
            }
        }
    );
}
