#include "service.h"

#include "syscall.h"
#include "../types/fixed_array.h"

#define MAX_SERVICE_MAPPED_MEMORY 256

typedef struct ServiceMappedMemoryRequest
{
    Address physical_address;
    size_t size;
    Address driver_address;
    Address device_address;
}
ServiceMappedMemoryRequest;

static ServiceMappedMemoryRequest ServiceMappedMemoryRequest__new(Address physical_address, size_t size)
{
    return (ServiceMappedMemoryRequest) {
        .physical_address = physical_address,
        .size = size,
        .driver_address = 0,
        .device_address = physical_address,
    };
}

struct ServiceMappedMemory
{
    Address driver_address;
    Address device_address;
    size_t size;
};

static ServiceMappedMemory ServiceMappedMemory__new(Address driver_address, Address device_address, size_t size)
{
    return (ServiceMappedMemory) {
        .driver_address = driver_address,
        .device_address = device_address,
        .size = size,
    };
}

OptionalDef(ServiceMappedMemory);

static bool ServiceMappedMemory__equals(void * self, void * other)
{
    ServiceMappedMemoryPtr left = (ServiceMappedMemoryPtr) self;
    ServiceMappedMemoryPtr right = (ServiceMappedMemoryPtr) other;
    return left->driver_address == right->driver_address
        && left->device_address == right->device_address
        && left->size == right->size;
}

FixedArrayDefine(ServiceMappedMemory, MAX_SERVICE_MAPPED_MEMORY, ServiceMappedMemoryArray, ServiceMappedMemory__equals);
FixedArrayBodyDefine(ServiceMappedMemoryArray);

static ServiceMappedMemoryArray mapped_memory = EmptyArray(ServiceMappedMemoryArray);

ServiceCode service_wait(ServiceEventPtr event)
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_WAIT, (Anything) event, 0, 0, 0);
}

ServiceCode service_become_service()
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_BECOME_SERVICE, 0, 0, 0, 0);
}

ServiceCode service_map_mmio(Address physical_address, size_t size, ServiceMappedMemoryPtr * memory)
{
    if (memory == 0)
    {
        return SERVICE_INVALID_BUFFER;
    }

    ServiceMappedMemoryRequest request = ServiceMappedMemoryRequest__new(physical_address, size);

    ServiceCode result = (ServiceCode) syscall(SERVICE_CALL, SERVICE_MAP_MMIO, (Anything) &request, 0, 0, 0);
    if (result != SERVICE_OK)
    {
        return result;
    }

    ServiceMappedMemory target =
        ServiceMappedMemory__new(request.driver_address, request.device_address, request.size);

    match_result(CollectionCode, ServiceMappedMemoryArray__push(&mapped_memory, target),
        success(index)
        {
            match_optional(ServiceMappedMemoryArray__type_ptr, ServiceMappedMemoryArray__ptr_at(&mapped_memory, index),
                some(stored)
                {
                    *memory = stored;
                    return SERVICE_OK;
                },
                none_do_nothing()
            );
        },
        error(code, message) { return SERVICE_INVALID_BUFFER; }
    );

    return SERVICE_INVALID_BUFFER;
}

ServiceCode service_subscribe_irq(uint64_t irq)
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_SUBSCRIBE_IRQ, irq, 0, 0, 0);
}

ServiceCode service_register_block_device(ConstBStringPtr path, uint64_t block_size, uint64_t block_count)
{
    ServiceBlockDeviceRegistration request =
        ServiceBlockDeviceRegistration__new(path, block_size, block_count);
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &request, 0, 0, 0);
}

ServiceBlockReadResult service_block_read_device(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
)
{
    ServiceBlockReadRequest request =
        ServiceBlockReadRequest__new(path, block, block_count, buffer, buffer_size);
    return (ServiceBlockReadResult) syscall(SERVICE_CALL, SERVICE_BLOCK_READ_DEVICE, (Anything) &request, 0, 0, 0);
}

ServiceBlockReadResult service_block_write_device(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
)
{
    ServiceBlockReadRequest request =
        ServiceBlockReadRequest__new(path, block, block_count, buffer, buffer_size);
    return (ServiceBlockReadResult) syscall(SERVICE_CALL, SERVICE_BLOCK_WRITE_DEVICE, (Anything) &request, 0, 0, 0);
}

ServiceCode service_block_get_request(uint64_t request_id, ServiceBlockRequestPtr request)
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_BLOCK_GET_REQUEST, request_id, (Anything) request, 0, 0);
}

ServiceCode service_block_complete(uint64_t request_id, ServiceCode status, uint64_t block_count)
{
    ServiceBlockCompletion completion = ServiceBlockCompletion__new(request_id, status, block_count);

    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_BLOCK_COMPLETE, (Anything) &completion, 0, 0, 0);
}

ServiceCode service_register_filesystem(
    ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint
)
{
    ServiceFilesystemRegistration request =
        ServiceFilesystemRegistration__new(device_path, filesystem_type, mountpoint);

    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_REGISTER_FILESYSTEM, (Anything) &request, 0, 0, 0);
}

ServiceCode service_filesystem_get_request(uint64_t request_id, ServiceFilesystemRequestPtr request)
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_FILESYSTEM_GET_REQUEST, request_id, (Anything) request, 0, 0);
}

ServiceCode service_filesystem_complete(uint64_t request_id, ServiceCode status, int64_t result)
{
    ServiceFilesystemCompletion completion = ServiceFilesystemCompletion__new(request_id, status, result);

    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_FILESYSTEM_COMPLETE, (Anything) &completion, 0, 0, 0);
}

ServiceCode service_filesystem_complete_open(
    uint64_t request_id, ServiceCode status, int64_t result, FileMode mode, uint64_t size, uint32_t * blocks
)
{
    ServiceFilesystemCompletion completion = ServiceFilesystemCompletion__new(request_id, status, result);
    completion.mode = mode;
    completion.size = size;
    if (blocks != 0)
    {
        for (size_t i = 0; i < 12; i++)
        {
            completion.block[i] = blocks[i];
        }
    }

    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_FILESYSTEM_COMPLETE, (Anything) &completion, 0, 0, 0);
}

ServiceCode service_device_address(void * driver_address, Address * device_address)
{
    if (driver_address == 0 || device_address == 0)
    {
        return SERVICE_INVALID_BUFFER;
    }

    ServiceDeviceAddressRequest request = ServiceDeviceAddressRequest__new((Address) driver_address);

    ServiceCode result = (ServiceCode) syscall(SERVICE_CALL, SERVICE_DEVICE_ADDRESS, (Anything) &request, 0, 0, 0);
    if (result != SERVICE_OK)
    {
        return result;
    }

    *device_address = request.device_address;
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    if (name == 0)
    {
        return SERVICE_INVALID_BUFFER;
    }

    ServiceReadyRequest request = ServiceReadyRequest__new(name);

    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_PUBLISH_READY, (Anything) &request, 0, 0, 0);
}

ServiceCode service_wait_ready(ConstBStringPtr name)
{
    if (name == 0)
    {
        return SERVICE_INVALID_BUFFER;
    }

    ServiceReadyRequest request = ServiceReadyRequest__new(name);

    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_WAIT_READY, (Anything) &request, 0, 0, 0);
}

void * driver_ptr(ServiceMappedMemoryPtr memory)
{
    if (memory == 0)
    {
        return 0;
    }
    return (void *) memory->driver_address;
}

Address device_ptr(ServiceMappedMemoryPtr memory)
{
    if (memory == 0)
    {
        return 0;
    }
    return memory->device_address;
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    if (memory == 0 || offset >= memory->size)
    {
        return 0;
    }
    return (void *) (memory->driver_address + offset);
}

Address device_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    if (memory == 0 || offset >= memory->size)
    {
        return 0;
    }
    return memory->device_address + offset;
}

size_t mapped_size(ServiceMappedMemoryPtr memory)
{
    if (memory == 0)
    {
        return 0;
    }
    return memory->size;
}
