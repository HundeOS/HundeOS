#ifndef _COMMON_INCLUDES_SYSCALLS_SERVICE_H
#define _COMMON_INCLUDES_SYSCALLS_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "types/address.h"
#include "types/bstring.h"
#include "ipc/queue_types.h"
#include "../../common/ipc/block_protocol.h"

typedef enum ServiceOperation
{
    SERVICE_BECOME_SERVICE,
    SERVICE_WAIT,
    SERVICE_MAP_MMIO,
    SERVICE_SUBSCRIBE_IRQ,
    SERVICE_REGISTER_BLOCK_DEVICE, // Discovery metadata only; I/O uses Channels.
    /* Retired block transport operations retain their ABI numbers. */
    SERVICE_BLOCK_GET_REQUEST,
    SERVICE_BLOCK_COMPLETE,
    SERVICE_BLOCK_READ_DEVICE,
    SERVICE_BLOCK_WRITE_DEVICE,
    SERVICE_DEVICE_ADDRESS,
    SERVICE_PUBLISH_READY,
    SERVICE_WAIT_READY,
    /* Operations 12-14 were the retired filesystem broker. */
    SERVICE_BLOCK_READ_DEVICE_ASYNC = 15,
    SERVICE_BLOCK_WRITE_DEVICE_ASYNC,
    /* Operations 17-19 were the retired socket broker. */
    SERVICE_BLOCK_CONNECT = 20,
    SERVICE_BLOCK_DISCONNECT,
    SERVICE_QUEUE_LISTEN,
    SERVICE_QUEUE_CONNECT,
    SERVICE_QUEUE_ACCEPT,
    SERVICE_QUEUE_CLOSE,
    SERVICE_REGISTER_INTERRUPT_MANAGER,
    SERVICE_ROUTE_MANAGED_IRQ,
    SERVICE_POLL,
    SERVICE_AUTHORIZE_BLOCK_CLIENT,
    SERVICE_CLAIM_VIRTIO_DEVICE,
    SERVICE_BOOT_HARDWARE_INFO,
    SERVICE_QUEUE_WAIT,
    SERVICE_QUEUE_WAIT_CONTROL,
    SERVICE_AUTHORIZE_MOUNT_CLIENT,
    SERVICE_QUEUE_DATA_AVAILABLE,
} ServiceOperation;

typedef enum ServiceEventType
{
    SERVICE_EVENT_MESSAGE = 1,
    SERVICE_EVENT_IRQ = 2,
    SERVICE_EVENT_IO = 3,
    SERVICE_EVENT_BLOCK_IO = 4,
    SERVICE_EVENT_READY = 5,
    SERVICE_EVENT_BLOCK_CONNECTION_OPENED = 8,
    SERVICE_EVENT_BLOCK_CONNECTION_CLOSED = 9,
} ServiceEventType;

typedef enum ServiceEventKind
{
    SERVICE_EVENT_REQUEST = 1,
    SERVICE_EVENT_RESPONSE = 2,
} ServiceEventKind;

#define SERVICE_EVENT_RESPONSE_SOURCE_FLAG (1ULL << 63)
#define SERVICE_EVENT_SOURCE_MASK (~SERVICE_EVENT_RESPONSE_SOURCE_FLAG)

typedef enum ServiceIOEvent
{
    SERVICE_IO_READ_REQUEST = 1,
    SERVICE_IO_WRITE_REQUEST = 2,
} ServiceIOEvent;

typedef struct ServiceEvent
{
    ServiceEventType type;
    uint64_t source;
    uint64_t data;
} ServiceEvent;

typedef ServiceEvent * ServiceEventPtr;
typedef const ServiceEvent * ConstServiceEventPtr;

static inline ServiceEvent ServiceEvent__new(ServiceEventType type, uint64_t source, uint64_t data)
{
    return (ServiceEvent){
        .type = type,
        .source = source,
        .data = data,
    };
}

static inline ServiceEvent ServiceEvent__request(ServiceEventType type, uint64_t request_id, uint64_t data)
{
    return (ServiceEvent){
        .type = type,
        .source = request_id & SERVICE_EVENT_SOURCE_MASK,
        .data = data,
    };
}

static inline ServiceEvent ServiceEvent__response(ServiceEventType type, uint64_t request_id, uint64_t data)
{
    return (ServiceEvent){
        .type = type,
        .source = (request_id & SERVICE_EVENT_SOURCE_MASK) | SERVICE_EVENT_RESPONSE_SOURCE_FLAG,
        .data = data,
    };
}

static inline ServiceEventKind ServiceEvent__kind(ServiceEventPtr self)
{
    if ((self->source & SERVICE_EVENT_RESPONSE_SOURCE_FLAG) != 0)
    {
        return SERVICE_EVENT_RESPONSE;
    }

    return SERVICE_EVENT_REQUEST;
}

static inline uint64_t ServiceEvent__request_id(ServiceEventPtr self)
{
    return self->source & SERVICE_EVENT_SOURCE_MASK;
}

typedef enum ServiceCode
{
    SERVICE_OK = 0,
    SERVICE_COMPLETE = 1,
    SERVICE_ABORT = 2,
    SERVICE_NOT_SERVICE_PROCESS = -1,
    SERVICE_EVENT_QUEUE_EMPTY = -2,
    SERVICE_EVENT_QUEUE_FULL = -3,
    SERVICE_SCHEDULER_ERROR = -4,
    SERVICE_INVALID_BUFFER = -5,
    SERVICE_OPERATION_NOT_SUPPORTED = -6,
    SERVICE_MAP_FAILED = -7,
    SERVICE_IRQ_ALREADY_REGISTERED = -8,
    SERVICE_IRQ_INVALID_CONSUMER = -9,
    SERVICE_IRQ_REGISTRATION_FAILED = -10,
    SERVICE_DEVICE_ALREADY_REGISTERED = -11,
    SERVICE_DEVICE_REGISTRATION_FAILED = -12,
    SERVICE_BLOCK_REQUEST_NOT_FOUND = -13,
    SERVICE_BLOCK_REQUEST_FAILED = -14,
    SERVICE_PERMISSION_DENIED = -15,
    SERVICE_BLOCK_CONNECTION_NOT_FOUND = -20,
    SERVICE_BLOCK_CONNECTION_FAILED = -21,
    SERVICE_INTERRUPT_MANAGER_EXISTS = -22,
    SERVICE_INTERRUPT_MANAGER_REQUIRED = -23,
} ServiceCode;

typedef struct ServiceInterruptManagerRegistration
{
    Address event_queue;
    Address distributor;
} ServiceInterruptManagerRegistration;

typedef ServiceInterruptManagerRegistration * ServiceInterruptManagerRegistrationPtr;

typedef struct ServiceManagedIRQRoute
{
    uint64_t client_pid;
    uint64_t irq;
} ServiceManagedIRQRoute;

typedef ServiceManagedIRQRoute * ServiceManagedIRQRoutePtr;

static inline ServiceManagedIRQRoute ServiceManagedIRQRoute__new(uint64_t client_pid, uint64_t irq)
{
    return (ServiceManagedIRQRoute){.client_pid = client_pid, .irq = irq};
}

typedef struct ServiceMappedMemory ServiceMappedMemory;
typedef ServiceMappedMemory * ServiceMappedMemoryPtr;
OptionalDef(ServiceMappedMemoryPtr);

typedef struct ServiceDeviceAddressRequest
{
    Address driver_address;
    Address device_address;
} ServiceDeviceAddressRequest;

typedef ServiceDeviceAddressRequest * ServiceDeviceAddressRequestPtr;

typedef struct ServiceBlockDeviceRegistration
{
    BString path;
    uint64_t block_size;
    uint64_t block_count;
} ServiceBlockDeviceRegistration;

typedef ServiceBlockDeviceRegistration * ServiceBlockDeviceRegistrationPtr;

typedef struct ServiceReadyRequest
{
    BString name;
} ServiceReadyRequest;

typedef ServiceReadyRequest * ServiceReadyRequestPtr;

static inline ServiceBlockDeviceRegistration
ServiceBlockDeviceRegistration__new(ConstBStringPtr path, uint64_t block_size, uint64_t block_count)
{
    return (ServiceBlockDeviceRegistration){
        .path = *path,
        .block_size = block_size,
        .block_count = block_count,
    };
}

static inline ServiceDeviceAddressRequest ServiceDeviceAddressRequest__new(Address driver_address)
{
    return (ServiceDeviceAddressRequest){
        .driver_address = driver_address,
        .device_address = driver_address,
    };
}

static inline ServiceReadyRequest ServiceReadyRequest__new(ConstBStringPtr name)
{
    return (ServiceReadyRequest){
        .name = *name,
    };
}

#endif
