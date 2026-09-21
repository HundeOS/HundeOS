#include "service_directory.h"

ServiceDirectoryPtr ServiceDirectory__shared(void)
{
    return (ServiceDirectoryPtr)HUND_SERVICE_DIRECTORY_VADDR;
}

bool ServiceDirectoryEntry__name_equals(ServiceDirectoryEntryPtr self, ConstBStringPtr name)
{
    return BString__equals(&self->name, name);
}

OptionalDef(ServiceDirectoryEntryPtr);

static Optional(ServiceDirectoryEntryPtr)
    ServiceDirectory__find_protected(ServiceDirectoryPtr self, ConstBStringPtr name)
{
    for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
    {
        ServiceDirectoryEntryPtr entry = &self->entries[index];
        if (__atomic_load_n(&entry->present, __ATOMIC_ACQUIRE) != 0 && ServiceDirectoryEntry__name_equals(entry, name))
        {
            return Some(ServiceDirectoryEntryPtr, entry);
        }
    }
    return None(ServiceDirectoryEntryPtr);
}

Optional(QueueEndpointID) ServiceDirectory__find_endpoint(ServiceDirectoryPtr self, ConstBStringPtr name)
{
    match_optional(
        ServiceDirectory__find_protected(self, name),
        some(entry)
        {
            return Some(QueueEndpointID, __atomic_load_n(&entry->generation, __ATOMIC_ACQUIRE));
        },
        none_do_nothing()
    );
    return None(QueueEndpointID);
}

Optional(uint64_t) ServiceDirectory__find_service_pid(ServiceDirectoryPtr self, ConstBStringPtr name)
{
    match_optional(
        ServiceDirectory__find_protected(self, name),
        some(entry)
        {
            return Some(uint64_t, __atomic_load_n(&entry->service_pid, __ATOMIC_ACQUIRE));
        },
        none_do_nothing()
    );
    return None(uint64_t);
}
