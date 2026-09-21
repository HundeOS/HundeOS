#include "service_directory.h"

#include "../arch/page_table.h"
#include "../kalloc.h"
#include "../process.h"

static BString service_directory_allocation_failed = bstr("Service directory allocation failed");
static BString service_directory_not_initialized = bstr("Service directory not initialized");
static BString service_directory_full = bstr("Service directory full");
static BString service_directory_invalid_name = bstr("Invalid service name");

static void KernelServiceDirectory__copy_name(ServiceDirectoryEntryPtr entry, ConstBStringPtr name)
{
    entry->name = BString__slice(name, 0, BString__length(name));
}

ServiceDirectoryPtr KernelServiceDirectory__kernel_directory(KernelServiceDirectoryPtr self)
{
    return k_v_ptr(self->directory, ServiceDirectory);
}

Result(ServiceCode) KernelServiceDirectory__init(KernelServiceDirectoryPtr self)
{
    match_result(
        Kernel__try_alloc_user(HUND_SERVICE_DIRECTORY_VADDR, HUND_SERVICE_DIRECTORY_SIZE, true, false, false),
        success(directory)
        {
            self->directory = directory;
            Kernel__memset(self->directory.kernel_addr.virtual_addr, 0, HUND_SERVICE_DIRECTORY_SIZE);
            ServiceDirectoryPtr service_directory = KernelServiceDirectory__kernel_directory(self);
            service_directory->magic = HUND_SERVICE_DIRECTORY_MAGIC;
            service_directory->version = HUND_SERVICE_DIRECTORY_VERSION;
            return Success(ServiceCode, SERVICE_OK);
        },
        error(code, message)
        {
            return Error(ServiceCode, service_directory_allocation_failed, SERVICE_MAP_FAILED);
        }
    );

    return Error(ServiceCode, service_directory_allocation_failed, SERVICE_MAP_FAILED);
}

Result(ServiceCode) KernelServiceDirectory__map_process(KernelServiceDirectoryPtr self, ProcessPtr process)
{
    if (!self->directory.valid)
    {
        return Error(ServiceCode, service_directory_not_initialized, SERVICE_MAP_FAILED);
    }

    PageTable__map_shared(&process->page_table, process->kernel, self->directory, HUND_SERVICE_DIRECTORY_SIZE);
    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) KernelServiceDirectory__publish_endpoint(KernelServiceDirectoryPtr self, ProcessPtr process,
                                                             ConstBStringPtr name, QueueEndpointID endpoint)
{
    if (!self->directory.valid || BString__is_empty(name) || BString__length(name) >= (StringPos)HUND_SERVICE_NAME_SIZE)
    {
        return Error(ServiceCode, service_directory_invalid_name, SERVICE_INVALID_BUFFER);
    }
    critical_section(
        self->lock,
        {
            ServiceDirectoryPtr directory = KernelServiceDirectory__kernel_directory(self);
            for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
            {
                ServiceDirectoryEntryPtr entry = &directory->entries[index];
                if (__atomic_load_n(&entry->present, __ATOMIC_ACQUIRE) != 0 &&
                    ServiceDirectoryEntry__name_equals(entry, name))
                {
                    cs_return Error(ServiceCode, service_directory_full,
                                    SERVICE_DEVICE_ALREADY_REGISTERED);
                }
            }
            for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
            {
                ServiceDirectoryEntryPtr entry = &directory->entries[index];
                if (__atomic_load_n(&entry->present, __ATOMIC_ACQUIRE) != 0)
                {
                    continue;
                }
                KernelServiceDirectory__copy_name(entry, name);
                entry->service_pid = process->pid;
                entry->generation = endpoint;
                directory->entry_count++;
                directory->generation++;
                __atomic_store_n(&entry->present, 1, __ATOMIC_RELEASE);
                cs_return Success(ServiceCode, SERVICE_OK);
            }
        }
    );
    return Error(ServiceCode, service_directory_full, SERVICE_EVENT_QUEUE_FULL);
}

void KernelServiceDirectory__remove_endpoint(KernelServiceDirectoryPtr self, QueueEndpointID endpoint)
{
    if (!self->directory.valid)
    {
        return;
    }
    critical_section(
        self->lock,
        {
            ServiceDirectoryPtr directory = KernelServiceDirectory__kernel_directory(self);
            for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
            {
                ServiceDirectoryEntryPtr entry = &directory->entries[index];
                if (__atomic_load_n(&entry->present, __ATOMIC_ACQUIRE) != 0 &&
                    entry->generation == endpoint)
                {
                    __atomic_store_n(&entry->present, 0, __ATOMIC_RELEASE);
                    directory->entry_count--;
                    directory->generation++;
                }
            }
        }
    );
}
