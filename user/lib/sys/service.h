#ifndef __USER_SYS_SERVICE_H
#define __USER_SYS_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "syscalls/service.h"
#include "syscalls/socket.h"

ServiceCode service_become_service();
ServiceCode service_wait(ServiceEventPtr event);
ServiceCode service_map_mmio(Address physical_address, size_t size, ServiceMappedMemoryPtr * memory);
ServiceCode service_subscribe_irq(uint64_t irq);
ServiceCode service_register_block_device(ConstBStringPtr path, uint64_t block_size, uint64_t block_count);
ServiceBlockReadResult service_block_read_device(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
);
ServiceBlockReadResult service_block_write_device(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
);
ServiceBlockReadResult service_block_read_device_async(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
);
ServiceBlockReadResult service_block_write_device_async(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
);
ServiceCode service_block_get_request(uint64_t request_id, ServiceBlockRequestPtr request);
ServiceCode service_block_complete(uint64_t request_id, ServiceCode status, uint64_t block_count);
ServiceCode service_register_filesystem(
    ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint
);
ServiceCode service_filesystem_get_request(uint64_t request_id, ServiceFilesystemRequestPtr request);
ServiceCode service_filesystem_complete(uint64_t request_id, ServiceCode status, int64_t result);
ServiceCode service_filesystem_complete_open(
    uint64_t request_id, ServiceCode status, int64_t result, FileMode mode, uint64_t size, uint32_t * blocks
);
ServiceCode service_filesystem_complete_open_without_blocks(
    uint64_t request_id, ServiceCode status, int64_t result, FileMode mode, uint64_t size
);
ServiceCode service_register_socket_router(void);
ServiceCode service_socket_get_request(uint64_t request_id, IOSocketRouterRequestPtr request);
ServiceCode service_socket_complete(uint64_t request_id, IOSocketStatus status, IOHandle result);
ServiceCode service_device_address(void * driver_address, Address * device_address);
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
