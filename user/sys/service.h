#ifndef __USER_SYS_SERVICE_H
#define __USER_SYS_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "syscalls/service.h"

ServiceCode service_become_service();
ServiceCode service_wait(ServiceEventPtr event);
ServiceCode service_map_mmio(uint64_t physical_address, uint64_t size, ServiceMappedMemoryPtr * memory);
ServiceCode service_subscribe_irq(uint64_t irq);
ServiceCode service_register_block_device(const char * path, uint64_t block_size, uint64_t block_count);
ServiceCode service_block_get_request(uint64_t request_id, ServiceBlockRequestPtr request);
ServiceCode service_block_complete(uint64_t request_id, ServiceCode status, uint64_t block_count);
ServiceCode service_device_address(void * driver_address, uint64_t * device_address);
ServiceCode service_publish_ready(ConstBStringPtr name);
ServiceCode service_wait_ready(ConstBStringPtr name);
void * driver_ptr(ServiceMappedMemoryPtr memory);
uint64_t device_ptr(ServiceMappedMemoryPtr memory);
void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset);
uint64_t device_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset);
uint64_t mapped_size(ServiceMappedMemoryPtr memory);

static inline bool service_alive(ServiceCode wait_status)
{
    return wait_status != SERVICE_COMPLETE && wait_status != SERVICE_ABORT;
}

static inline bool service_ok(ServiceCode wait_status)
{
    return wait_status == SERVICE_OK;
}

#endif
