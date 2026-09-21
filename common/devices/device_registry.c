#include "device_registry.h"

DeviceRecord DeviceRecord__new(DeviceKind kind, uint64_t owner_pid, ConstBStringPtr path, ConstBStringPtr alias,
                               uint64_t block_size, uint64_t block_count)
{
    BString alias_path = BString__is_empty(alias) ? empty_bstr() : BString__sprintf(&bstr("/dev/%B"), alias);
    return (DeviceRecord){.kind = kind,
                          .owner_pid = owner_pid,
                          .path = *path,
                          .alias = alias_path,
                          .service = kind == DEVICE_BLOCK ? *path : empty_bstr(),
                          .block_size = block_size,
                          .block_count = block_count};
}

ConstDeviceRegistryPtr DeviceRegistry__shared(void)
{
    return (ConstDeviceRegistryPtr)DEVICE_REGISTRY_VADDR;
}

Result(DeviceRecord) DeviceRegistry__at(ConstDeviceRegistryPtr self, uint64_t index)
{
    if (self->version != DEVICE_REGISTRY_VERSION || index >= DEVICE_REGISTRY_CAPACITY)
    {
        return Error(DeviceRecord, bstr("Invalid device registry"), DEVICE_REGISTRY_INVALID);
    }
    return DeviceRecords__at(&self->records, index);
}

Result(OptionalDeviceRecord) DeviceRegistry__find(ConstDeviceRegistryPtr self, ConstBStringPtr path)
{
    for (uint64_t index = 0; index < DEVICE_REGISTRY_CAPACITY; index++)
    {
        match_result(
            DeviceRegistry__at(self, index),
            success(record)
            {
                if (record.kind != DEVICE_ABSENT &&
                    (BString__equals(&record.path, path) || BString__equals(&record.alias, path)))
                {
                    return Success(OptionalDeviceRecord, Some(DeviceRecord, record));
                }
            },
            error(code, message)
            {
                return Error(OptionalDeviceRecord, message, code);
            }
        );
    }
    return Success(OptionalDeviceRecord, None(DeviceRecord));
}

Result(CollectionCode) DeviceRegistry__set(DeviceRegistryPtr self, uint64_t index, ConstDeviceRecordPtr record)
{
    return DeviceRecords__set(&self->records, index, record);
}
