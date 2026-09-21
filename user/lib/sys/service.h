#ifndef __USER_SYS_SERVICE_H
#define __USER_SYS_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "syscalls/service.h"
#include "syscalls/socket.h"

ServiceCode service_become_service();
ServiceCode service_wait(ServiceEventPtr event);
ServiceCode service_poll(ServiceEventPtr event);
ServiceCode service_map_mmio(Address physical_address, size_t size, ServiceMappedMemoryPtr * memory);
ServiceCode service_subscribe_irq(uint64_t irq);
ServiceCode service_register_interrupt_manager(ServiceInterruptManagerRegistrationPtr registration);
ServiceCode service_route_managed_irq(uint64_t client_pid, uint64_t irq);
ServiceCode service_register_block_device(ConstBStringPtr path, uint64_t block_size, uint64_t block_count);
ServiceCode service_claim_virtio_device(Address mapping);
ServiceCode service_authorize_block_client(uint64_t client_pid);
ServiceCode service_authorize_mount_client(uint64_t client_pid);
ServiceCode service_device_address(void * driver_address, AddressPtr device_address);
ServiceCode service_publish_ready(ConstBStringPtr name);
ServiceCode service_wait_ready(ConstBStringPtr name);
void * driver_ptr(ServiceMappedMemoryPtr memory);
Address device_ptr(ServiceMappedMemoryPtr memory);
void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset);
Address device_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset);
size_t mapped_size(ServiceMappedMemoryPtr memory);

static inline bool service_alive(ServiceCode wait_status)
{
    return wait_status != SERVICE_COMPLETE && wait_status != SERVICE_ABORT;
}

static inline bool service_ok(ServiceCode wait_status)
{
    return wait_status == SERVICE_OK;
}

#endif
