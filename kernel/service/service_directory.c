#include "service_directory.h"

#include "../arch/page_table.h"
#include "../kalloc.h"
#include "../process.h"

static BString service_directory_allocation_failed = bstr("Service directory allocation failed");
static BString service_directory_not_initialized = bstr("Service directory not initialized");
static BString service_directory_full = bstr("Service directory full");
static BString service_directory_invalid_name = bstr("Invalid service name");
static BString service_directory_invalid_subscription = bstr("Invalid service kernel event subscription");
static BString service_directory_queue_full = bstr("Service queue full");

static size_t KernelServiceDirectory__page_aligned_size(size_t size)
{
    return (size + (HUND_SERVICE_PAGE_SIZE - 1)) & ~(HUND_SERVICE_PAGE_SIZE - 1);
}

static void KernelServiceDirectory__copy_name(ServiceDirectoryEntryPtr entry, ConstBStringPtr name)
{
    ServiceDirectoryName__copy_from_bstring(&entry->name, name);
}

ServiceDirectoryPtr KernelServiceDirectory__kernel_directory(KernelServiceDirectoryPtr self)
{
    return k_v_ptr(self->directory, ServiceDirectory);
}

ServiceQueuePtr KernelServiceDirectory__kernel_queue(KernelServiceDirectoryPtr self, uint64_t index)
{
    return ptr(self->queues.kernel_addr.virtual_addr + (index * HUND_SERVICE_PAGE_SIZE), ServiceQueue);
}

Result(ServiceCode) KernelServiceDirectory__init(KernelServiceDirectoryPtr self)
{
    size_t directory_size = KernelServiceDirectory__page_aligned_size(sizeof(ServiceDirectory));
    size_t queue_region_size = KernelServiceDirectory__page_aligned_size(HUND_SERVICE_SHARED_REGION_SIZE);

    match_result(
        Kernel__try_alloc_user(HUND_SERVICE_DIRECTORY_VADDR, directory_size, true, false, false),
        success(directory)
        {
            match_result(
                Kernel__try_alloc_user(HUND_SERVICE_QUEUE_REGION_VADDR, queue_region_size, true, true, false),
                success(queues)
                {
                    self->directory = directory;
                    self->queues = queues;
                    Kernel__memset(self->directory.kernel_addr.virtual_addr, 0, directory_size);
                    Kernel__memset(self->queues.kernel_addr.virtual_addr, 0, queue_region_size);
                    ServiceDirectoryPtr service_directory = KernelServiceDirectory__kernel_directory(self);
                    service_directory->magic = HUND_SERVICE_DIRECTORY_MAGIC;
                    service_directory->version = HUND_SERVICE_DIRECTORY_VERSION;
                    return Success(ServiceCode, SERVICE_OK);
                },
                error(code, message)
                {
                    Kernel__free(directory.kernel_addr.virtual_addr);
                    return Error(ServiceCode, service_directory_allocation_failed, SERVICE_MAP_FAILED);
                }
            );
        },
        error(code, message) { return Error(ServiceCode, service_directory_allocation_failed, SERVICE_MAP_FAILED); }
    );

    return Error(ServiceCode, service_directory_allocation_failed, SERVICE_MAP_FAILED);
}

Result(ServiceCode) KernelServiceDirectory__map_process(KernelServiceDirectoryPtr self, ProcessPtr process)
{
    if (!self->directory.valid || !self->queues.valid)
    {
        return Error(ServiceCode, service_directory_not_initialized, SERVICE_MAP_FAILED);
    }

    PageTable__map(&process->page_table, process->kernel, self->directory, HUND_SERVICE_DIRECTORY_SIZE);
    PageTable__map(&process->page_table, process->kernel, self->queues, HUND_SERVICE_SHARED_REGION_SIZE);
    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) KernelServiceDirectory__request_queue(KernelServiceDirectoryPtr self, ProcessPtr process,
                                                          ConstBStringPtr name, uint64_t kernel_event_subscriptions)
{
    if (!self->directory.valid || !self->queues.valid)
    {
        return Error(ServiceCode, service_directory_not_initialized, SERVICE_MAP_FAILED);
    }
    if (BString__is_empty(name) || BString__length(name) >= (StringPos)HUND_SERVICE_NAME_SIZE)
    {
        return Error(ServiceCode, service_directory_invalid_name, SERVICE_INVALID_BUFFER);
    }
    if ((kernel_event_subscriptions & ~SERVICE_KERNEL_EVENT_SUPPORTED_MASK) != 0)
    {
        return Error(ServiceCode, service_directory_invalid_subscription, SERVICE_OPERATION_NOT_SUPPORTED);
    }

    bool registered = false;
    Result(ServiceCode) result = Error(ServiceCode, service_directory_full, SERVICE_EVENT_QUEUE_FULL);
    critical_section(self->lock, {
        ServiceDirectoryPtr directory = KernelServiceDirectory__kernel_directory(self);
        for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
        {
            ServiceDirectoryEntryPtr entry = &directory->entries[index];
            if (__atomic_load_n(&entry->present, __ATOMIC_ACQUIRE) != 0 &&
                ServiceDirectoryEntry__name_equals(entry, name))
            {
                if (entry->queue_size == 0)
                {
                    cs_return Error(ServiceCode, service_directory_full, SERVICE_DEVICE_ALREADY_REGISTERED);
                }
                entry->service_pid = process->pid;
                entry->kernel_event_subscriptions = kernel_event_subscriptions;
                entry->generation++;
                directory->generation++;
                result = Success(ServiceCode, SERVICE_OK);
                registered = true;
                break;
            }
        }

        for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
        {
            if (registered)
            {
                break;
            }

            ServiceDirectoryEntryPtr entry = &directory->entries[index];
            if (__atomic_load_n(&entry->present, __ATOMIC_ACQUIRE) == 0)
            {
                ServiceQueuePtr queue = KernelServiceDirectory__kernel_queue(self, index);
                Kernel__memset(addr(queue), 0, HUND_SERVICE_PAGE_SIZE);
                KernelServiceDirectory__copy_name(entry, name);
                entry->service_pid = process->pid;
                entry->queue_address = HUND_SERVICE_QUEUE_REGION_VADDR + (index * HUND_SERVICE_PAGE_SIZE);
                entry->queue_size = HUND_SERVICE_PAGE_SIZE;
                entry->kernel_event_subscriptions = kernel_event_subscriptions;
                entry->generation = directory->generation + 1;
                directory->entry_count++;
                directory->generation++;
                __atomic_store_n(&entry->present, 1, __ATOMIC_RELEASE);
                result = Success(ServiceCode, SERVICE_OK);
                registered = true;
                break;
            }
        }
    });

    return result;
}

Result(ServiceCode)
    KernelServiceDirectory__publish_endpoint(KernelServiceDirectoryPtr self, ProcessPtr process, ConstBStringPtr name,
                                             AtomicMessageQueueEndpointID endpoint)
{
    if (!self->directory.valid || BString__is_empty(name) || BString__length(name) >= (StringPos)HUND_SERVICE_NAME_SIZE)
    {
        return Error(ServiceCode, service_directory_invalid_name, SERVICE_INVALID_BUFFER);
    }
    critical_section(self->lock, {
        ServiceDirectoryPtr directory = KernelServiceDirectory__kernel_directory(self);
        for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
        {
            ServiceDirectoryEntryPtr entry = &directory->entries[index];
            if (__atomic_load_n(&entry->present, __ATOMIC_ACQUIRE) != 0 &&
                ServiceDirectoryEntry__name_equals(entry, name))
            {
                cs_return Error(ServiceCode, service_directory_full, SERVICE_DEVICE_ALREADY_REGISTERED);
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
            entry->queue_address = 0;
            entry->queue_size = 0;
            entry->kernel_event_subscriptions = 0;
            entry->generation = endpoint;
            directory->entry_count++;
            directory->generation++;
            __atomic_store_n(&entry->present, 1, __ATOMIC_RELEASE);
            cs_return Success(ServiceCode, SERVICE_OK);
        }
    });
    return Error(ServiceCode, service_directory_full, SERVICE_EVENT_QUEUE_FULL);
}

void KernelServiceDirectory__remove_endpoint(KernelServiceDirectoryPtr self, AtomicMessageQueueEndpointID endpoint)
{
    if (!self->directory.valid)
    {
        return;
    }
    critical_section(self->lock, {
        ServiceDirectoryPtr directory = KernelServiceDirectory__kernel_directory(self);
        for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
        {
            ServiceDirectoryEntryPtr entry = &directory->entries[index];
            if (__atomic_load_n(&entry->present, __ATOMIC_ACQUIRE) != 0 && entry->queue_size == 0 &&
                entry->generation == endpoint)
            {
                __atomic_store_n(&entry->present, 0, __ATOMIC_RELEASE);
                directory->entry_count--;
                directory->generation++;
            }
        }
    });
}

Result(ServiceCode)
    KernelServiceDirectory__notify_process_exit(KernelServiceDirectoryPtr self, uint64_t pid, ReturnCode return_code)
{
    if (!self->directory.valid || !self->queues.valid)
    {
        return Error(ServiceCode, service_directory_not_initialized, SERVICE_MAP_FAILED);
    }

    Result(ServiceCode) result = Success(ServiceCode, SERVICE_OK);
    critical_section(self->lock, {
        ServiceDirectoryPtr directory = KernelServiceDirectory__kernel_directory(self);
        for (uint64_t index = 0; index < HUND_SERVICE_MAX_SERVICES; index++)
        {
            ServiceDirectoryEntryPtr entry = &directory->entries[index];
            if (__atomic_load_n(&entry->present, __ATOMIC_ACQUIRE) != 0 &&
                (entry->kernel_event_subscriptions & SERVICE_KERNEL_EVENT_PROCESS_EXIT) != 0)
            {
                ServiceQueueMessage message = ServiceQueueMessage__process_exit(pid, (uint64_t)return_code);
                ServiceQueuePtr queue = KernelServiceDirectory__kernel_queue(self, index);
                ServiceQueueStatus queue_status = ServiceQueue__push(queue, &message);
                if (queue_status != SERVICE_QUEUE_OK)
                {
                    result = Error(ServiceCode, service_directory_queue_full, SERVICE_EVENT_QUEUE_FULL);
                    break;
                }
            }
        }
    });

    return result;
}
