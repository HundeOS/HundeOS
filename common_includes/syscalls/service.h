#ifndef _COMMON_INCLUDES_SYSCALLS_SERVICE_H
#define _COMMON_INCLUDES_SYSCALLS_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "types/address.h"
#include "types/bstring.h"
#include "ipc/queue_types.h"
#include "../../common/ipc/legacy_block_transport.h"

typedef enum ServiceOperation
{
    SERVICE_BECOME_SERVICE,
    SERVICE_WAIT,
    SERVICE_MAP_MMIO,
    SERVICE_SUBSCRIBE_IRQ,
    SERVICE_REGISTER_BLOCK_DEVICE,
    SERVICE_BLOCK_GET_REQUEST,
    SERVICE_BLOCK_COMPLETE,
    SERVICE_BLOCK_READ_DEVICE,
    SERVICE_BLOCK_WRITE_DEVICE,
    SERVICE_DEVICE_ADDRESS,
    SERVICE_PUBLISH_READY,
    SERVICE_WAIT_READY,
    SERVICE_REGISTER_FILESYSTEM,
    SERVICE_FILESYSTEM_GET_REQUEST,
    SERVICE_FILESYSTEM_COMPLETE,
    SERVICE_BLOCK_READ_DEVICE_ASYNC,
    SERVICE_BLOCK_WRITE_DEVICE_ASYNC,
    SERVICE_REGISTER_SOCKET_ROUTER,
    SERVICE_SOCKET_GET_REQUEST,
    SERVICE_SOCKET_COMPLETE,
    SERVICE_BLOCK_CONNECT,
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
} ServiceOperation;

typedef enum ServiceEventType
{
    SERVICE_EVENT_MESSAGE = 1,
    SERVICE_EVENT_IRQ = 2,
    SERVICE_EVENT_IO = 3,
    SERVICE_EVENT_BLOCK_IO = 4,
    SERVICE_EVENT_READY = 5,
    SERVICE_EVENT_FILESYSTEM_IO = 6,
    SERVICE_EVENT_SOCKET_IO = 7,
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
    SERVICE_FILESYSTEM_REQUEST_NOT_FOUND = -16,
    SERVICE_FILESYSTEM_REQUEST_FAILED = -17,
    SERVICE_SOCKET_REQUEST_NOT_FOUND = -18,
    SERVICE_SOCKET_REQUEST_FAILED = -19,
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

typedef enum ServiceFilesystemOperation
{
    SERVICE_FILESYSTEM_LIST_DIR = 1,
    SERVICE_FILESYSTEM_OPEN = 2,
    SERVICE_FILESYSTEM_READ = 3,
    SERVICE_FILESYSTEM_CLOSE = 4,
    SERVICE_FILESYSTEM_WRITE = 5,
    SERVICE_FILESYSTEM_MKDIR = 6,
    SERVICE_FILESYSTEM_REMOVE = 7,
} ServiceFilesystemOperation;

typedef enum ServiceBlockOperation
{
    SERVICE_BLOCK_READ = 1,
    SERVICE_BLOCK_WRITE = 2,
} ServiceBlockOperation;

typedef struct ServiceBlockRequest
{
    uint64_t request_id;
    BString device_path;
    ServiceBlockOperation operation;
    uint64_t block;
    uint64_t block_count;
    uint64_t block_size;
    Address buffer;
    size_t buffer_size;
} ServiceBlockRequest;

typedef ServiceBlockRequest * ServiceBlockRequestPtr;

static inline ServiceBlockRequest ServiceBlockRequest__new(uint64_t request_id, BString device_path,
                                                           ServiceBlockOperation operation, uint64_t block,
                                                           uint64_t block_count, uint64_t block_size, Address buffer,
                                                           size_t buffer_size)
{
    return (ServiceBlockRequest){
        .request_id = request_id,
        .device_path = device_path,
        .operation = operation,
        .block = block,
        .block_count = block_count,
        .block_size = block_size,
        .buffer = buffer,
        .buffer_size = buffer_size,
    };
}

typedef struct ServiceBlockCompletion
{
    uint64_t request_id;
    ServiceCode status;
    uint64_t block_count;
} ServiceBlockCompletion;

typedef ServiceBlockCompletion * ServiceBlockCompletionPtr;

typedef int64_t ServiceBlockReadResult;

typedef struct ServiceBlockReadRequest
{
    BString device_path;
    uint64_t block;
    uint64_t block_count;
    Address buffer;
    size_t buffer_size;
} ServiceBlockReadRequest;

typedef ServiceBlockReadRequest * ServiceBlockReadRequestPtr;

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

typedef struct ServiceBlockConnectionRequest
{
    BString device_path;
    BlockConnectionID connection_id;
    Address shared_memory;
    size_t shared_memory_size;
} ServiceBlockConnectionRequest;

typedef ServiceBlockConnectionRequest * ServiceBlockConnectionRequestPtr;

typedef struct ServiceBlockDisconnectionRequest
{
    BlockConnectionID connection_id;
} ServiceBlockDisconnectionRequest;

typedef ServiceBlockDisconnectionRequest * ServiceBlockDisconnectionRequestPtr;

typedef struct ServiceReadyRequest
{
    BString name;
} ServiceReadyRequest;

typedef ServiceReadyRequest * ServiceReadyRequestPtr;

typedef struct ServiceFilesystemRegistration
{
    BString device_path;
    BString filesystem_type;
    BString mountpoint;
} ServiceFilesystemRegistration;

typedef ServiceFilesystemRegistration * ServiceFilesystemRegistrationPtr;

typedef struct ServiceFilesystemRequest
{
    uint64_t request_id;
    ServiceFilesystemOperation operation;
    BString device_path;
    BString path;
    uint32_t handle;
    Flags flags;
    uint64_t block;
    uint64_t block_offset;
    Address buffer;
    size_t size;
    Address entries;
    size_t max_entries;
} ServiceFilesystemRequest;

typedef ServiceFilesystemRequest * ServiceFilesystemRequestPtr;

typedef struct ServiceFilesystemCompletion
{
    uint64_t request_id;
    ServiceCode status;
    int64_t result;
    FileMode mode;
    uint64_t size;
    uint32_t block[12];
} ServiceFilesystemCompletion;

typedef ServiceFilesystemCompletion * ServiceFilesystemCompletionPtr;

static inline ServiceBlockDeviceRegistration
ServiceBlockDeviceRegistration__new(ConstBStringPtr path, uint64_t block_size, uint64_t block_count)
{
    return (ServiceBlockDeviceRegistration){
        .path = *path,
        .block_size = block_size,
        .block_count = block_count,
    };
}

static inline ServiceBlockConnectionRequest ServiceBlockConnectionRequest__new(ConstBStringPtr path)
{
    return (ServiceBlockConnectionRequest){
        .device_path = *path,
        .connection_id = 0,
        .shared_memory = 0,
        .shared_memory_size = sizeof(BlockProtocolMemory),
    };
}

static inline ServiceBlockDisconnectionRequest ServiceBlockDisconnectionRequest__new(BlockConnectionID connection_id)
{
    return (ServiceBlockDisconnectionRequest){
        .connection_id = connection_id,
    };
}

static inline ServiceBlockCompletion ServiceBlockCompletion__new(uint64_t request_id, ServiceCode status,
                                                                 uint64_t block_count)
{
    return (ServiceBlockCompletion){
        .request_id = request_id,
        .status = status,
        .block_count = block_count,
    };
}

static inline ServiceBlockReadRequest ServiceBlockReadRequest__new(ConstBStringPtr path, uint64_t block,
                                                                   uint64_t block_count, Address buffer,
                                                                   size_t buffer_size)
{
    return (ServiceBlockReadRequest){
        .device_path = *path,
        .block = block,
        .block_count = block_count,
        .buffer = buffer,
        .buffer_size = buffer_size,
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

static inline ServiceFilesystemRegistration ServiceFilesystemRegistration__new(ConstBStringPtr device_path,
                                                                               ConstBStringPtr filesystem_type,
                                                                               ConstBStringPtr mountpoint)
{
    return (ServiceFilesystemRegistration){
        .device_path = *device_path,
        .filesystem_type = *filesystem_type,
        .mountpoint = *mountpoint,
    };
}

static inline ServiceFilesystemRequest
ServiceFilesystemRequest__new(uint64_t request_id, ServiceFilesystemOperation operation, ConstBStringPtr device_path,
                              ConstBStringPtr path, uint32_t handle, Flags flags, uint64_t block, uint64_t block_offset,
                              Address buffer, size_t size, Address entries, size_t max_entries)
{
    return (ServiceFilesystemRequest){
        .request_id = request_id,
        .operation = operation,
        .device_path = *device_path,
        .path = *path,
        .handle = handle,
        .flags = flags,
        .block = block,
        .block_offset = block_offset,
        .buffer = buffer,
        .size = size,
        .entries = entries,
        .max_entries = max_entries,
    };
}

static inline ServiceFilesystemCompletion ServiceFilesystemCompletion__new(uint64_t request_id, ServiceCode status,
                                                                           int64_t result)
{
    return (ServiceFilesystemCompletion){
        .request_id = request_id,
        .status = status,
        .result = result,
    };
}

#endif
