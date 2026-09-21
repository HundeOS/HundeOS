#include "service.h"

#include "syscall.h"
#include "types/fixed_array.h"

#define MAX_SERVICE_MAPPED_MEMORY 256

typedef struct ServiceMappedMemoryRequest
{
    Address physical_address;
    size_t size;
    Address driver_address;
    Address device_address;
} ServiceMappedMemoryRequest;

static ServiceMappedMemoryRequest ServiceMappedMemoryRequest__new(Address physical_address, size_t size)
{
    return (ServiceMappedMemoryRequest){
        .physical_address = physical_address,
        .size = size,
        .driver_address = physical_address,
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
    return (ServiceMappedMemory){
        .driver_address = driver_address,
        .device_address = device_address,
        .size = size,
    };
}

OptionalDef(ServiceMappedMemory);

static bool ServiceMappedMemory__equals(void * self, void * other)
{
    ServiceMappedMemoryPtr left = (ServiceMappedMemoryPtr)self;
    ServiceMappedMemoryPtr right = (ServiceMappedMemoryPtr)other;
    return left->driver_address == right->driver_address && left->device_address == right->device_address &&
           left->size == right->size;
}

FixedArrayDefine(ServiceMappedMemory, MAX_SERVICE_MAPPED_MEMORY, ServiceMappedMemoryArray, ServiceMappedMemory__equals);
FixedArrayBodyDefine(ServiceMappedMemoryArray);

static ServiceMappedMemoryArray mapped_memory = EmptyArray(ServiceMappedMemoryArray);

ServiceCode service_wait(ServiceEventPtr event)
{
    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_WAIT, (Anything)event, 0, 0, 0);
}

ServiceCode service_poll(ServiceEventPtr event)
{
    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_POLL, (Anything)event, 0, 0, 0);
}

ServiceCode service_become_service()
{
    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_BECOME_SERVICE, 0, 0, 0, 0);
}

ServiceCode service_map_mmio(Address physical_address, size_t size, ServiceMappedMemoryPtr * memory)
{
    ServiceMappedMemoryRequest request = ServiceMappedMemoryRequest__new(physical_address, size);

    ServiceCode result = (ServiceCode)syscall(SERVICE_CALL, SERVICE_MAP_MMIO, (Anything)&request, 0, 0, 0);
    if (result != SERVICE_OK)
    {
        return result;
    }

    ServiceMappedMemory target = ServiceMappedMemory__new(request.driver_address, request.device_address, request.size);

    match_result(
        ServiceMappedMemoryArray__push(&mapped_memory, target),
        success(index)
        {
            match_optional(
                ServiceMappedMemoryArray__ptr_at(&mapped_memory, index),
                some(stored)
                {
                    *memory = stored;
                    return SERVICE_OK;
                },
                none_do_nothing()
            );
        },
        error(code, message)
        {
            return SERVICE_INVALID_BUFFER;
        }
    );

    return SERVICE_INVALID_BUFFER;
}

ServiceCode service_subscribe_irq(uint64_t irq)
{
    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_SUBSCRIBE_IRQ, irq, 0, 0, 0);
}

ServiceCode service_register_interrupt_manager(ServiceInterruptManagerRegistrationPtr registration)
{
    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_REGISTER_INTERRUPT_MANAGER, (Anything)registration, 0, 0, 0);
}

ServiceCode service_route_managed_irq(uint64_t client_pid, uint64_t irq)
{
    ServiceManagedIRQRoute request = ServiceManagedIRQRoute__new(client_pid, irq);
    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_ROUTE_MANAGED_IRQ, (Anything)&request, 0, 0, 0);
}

ServiceCode service_claim_virtio_device(Address mapping)
{
    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_CLAIM_VIRTIO_DEVICE, (Anything)mapping, 0, 0, 0);
}

ServiceCode service_register_block_device(ConstBStringPtr path, uint64_t block_size, uint64_t block_count)
{
    ServiceBlockDeviceRegistration request = ServiceBlockDeviceRegistration__new(path, block_size, block_count);
    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&request, 0, 0, 0);
}

ServiceCode service_authorize_block_client(uint64_t client_pid)
{
    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_AUTHORIZE_BLOCK_CLIENT, client_pid, 0, 0, 0);
}

ServiceCode service_authorize_mount_client(uint64_t client_pid)
{
    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_AUTHORIZE_MOUNT_CLIENT, client_pid, 0, 0, 0);
}

ServiceCode service_device_address(void * driver_address, AddressPtr device_address)
{
    ServiceDeviceAddressRequest request = ServiceDeviceAddressRequest__new((Address)driver_address);

    ServiceCode result = (ServiceCode)syscall(SERVICE_CALL, SERVICE_DEVICE_ADDRESS, (Anything)&request, 0, 0, 0);
    if (result != SERVICE_OK)
    {
        return result;
    }

    *device_address = request.device_address;
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    ServiceReadyRequest request = ServiceReadyRequest__new(name);

    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_PUBLISH_READY, (Anything)&request, 0, 0, 0);
}

ServiceCode service_wait_ready(ConstBStringPtr name)
{
    ServiceReadyRequest request = ServiceReadyRequest__new(name);

    return (ServiceCode)syscall(SERVICE_CALL, SERVICE_WAIT_READY, (Anything)&request, 0, 0, 0);
}

void * driver_ptr(ServiceMappedMemoryPtr memory)
{
    return (void *)memory->driver_address;
}

Address device_ptr(ServiceMappedMemoryPtr memory)
{
    return memory->device_address;
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return (void *)(memory->driver_address + offset);
}

Address device_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return memory->device_address + offset;
}

size_t mapped_size(ServiceMappedMemoryPtr memory)
{
    return memory->size;
}
