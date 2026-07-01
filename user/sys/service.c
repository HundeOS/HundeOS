#include "service.h"

#include "syscall.h"

#define MAX_SERVICE_MAPPED_MEMORY 32

typedef struct ServiceMappedMemoryRequest
{
    uint64_t physical_address;
    uint64_t size;
    uint64_t driver_address;
    uint64_t device_address;
}
ServiceMappedMemoryRequest;

struct ServiceMappedMemory
{
    uint64_t driver_address;
    uint64_t device_address;
    uint64_t size;
};

static ServiceMappedMemory mapped_memory[MAX_SERVICE_MAPPED_MEMORY];
static uint64_t mapped_memory_count = 0;

static void service_copy_path(char target[SERVICE_DEVICE_PATH_MAX], const char * source)
{
    uint64_t i = 0;
    if (source != 0)
    {
        for (; i < SERVICE_DEVICE_PATH_MAX - 1 && source[i] != '\0'; i++)
        {
            target[i] = source[i];
        }
    }
    target[i] = '\0';
}

ServiceCode service_wait(ServiceEventPtr event)
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_WAIT, (Anything) event, 0, 0, 0);
}

ServiceCode service_become_service()
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_BECOME_SERVICE, 0, 0, 0, 0);
}

ServiceCode service_map_mmio(uint64_t physical_address, uint64_t size, ServiceMappedMemoryPtr * memory)
{
    if (memory == 0 || mapped_memory_count >= MAX_SERVICE_MAPPED_MEMORY)
    {
        return SERVICE_INVALID_BUFFER;
    }

    ServiceMappedMemoryRequest request = {
        .physical_address = physical_address,
        .size = size,
        .driver_address = 0,
        .device_address = physical_address,
    };

    ServiceCode result = (ServiceCode) syscall(SERVICE_CALL, SERVICE_MAP_MMIO, (Anything) &request, 0, 0, 0);
    if (result != SERVICE_OK)
    {
        return result;
    }

    ServiceMappedMemoryPtr target = &mapped_memory[mapped_memory_count++];
    target->driver_address = request.driver_address;
    target->device_address = request.device_address;
    target->size = request.size;
    *memory = target;
    return SERVICE_OK;
}

ServiceCode service_subscribe_irq(uint64_t irq)
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_SUBSCRIBE_IRQ, irq, 0, 0, 0);
}

ServiceCode service_register_block_device(const char * path, uint64_t block_size, uint64_t block_count)
{
    ServiceBlockDeviceRegistration request = {
        .block_size = block_size,
        .block_count = block_count,
    };
    service_copy_path(request.path, path);

    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &request, 0, 0, 0);
}

ServiceCode service_block_get_request(uint64_t request_id, ServiceBlockRequestPtr request)
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_BLOCK_GET_REQUEST, request_id, (Anything) request, 0, 0);
}

ServiceCode service_block_complete(uint64_t request_id, ServiceCode status, uint64_t block_count)
{
    ServiceBlockCompletion completion = {
        .request_id = request_id,
        .status = status,
        .block_count = block_count,
    };

    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_BLOCK_COMPLETE, (Anything) &completion, 0, 0, 0);
}

ServiceCode service_device_address(void * driver_address, uint64_t * device_address)
{
    if (driver_address == 0 || device_address == 0)
    {
        return SERVICE_INVALID_BUFFER;
    }

    ServiceDeviceAddressRequest request = {
        .driver_address = (uint64_t) driver_address,
        .device_address = 0,
    };

    ServiceCode result = (ServiceCode) syscall(SERVICE_CALL, SERVICE_DEVICE_ADDRESS, (Anything) &request, 0, 0, 0);
    if (result != SERVICE_OK)
    {
        return result;
    }

    *device_address = request.device_address;
    return SERVICE_OK;
}

void * driver_ptr(ServiceMappedMemoryPtr memory)
{
    if (memory == 0)
    {
        return 0;
    }
    return (void *) memory->driver_address;
}

uint64_t device_ptr(ServiceMappedMemoryPtr memory)
{
    if (memory == 0)
    {
        return 0;
    }
    return memory->device_address;
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    if (memory == 0 || offset > memory->size)
    {
        return 0;
    }
    return (void *) (memory->driver_address + offset);
}

uint64_t device_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    if (memory == 0 || offset > memory->size)
    {
        return 0;
    }
    return memory->device_address + offset;
}

uint64_t mapped_size(ServiceMappedMemoryPtr memory)
{
    if (memory == 0)
    {
        return 0;
    }
    return memory->size;
}
