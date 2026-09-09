#ifndef _COMMON_INCLUDES_SYSCALLS_SERVICE_H
#define _COMMON_INCLUDES_SYSCALLS_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "types/address.h"
#include "types/bstring.h"
#include "../../common/ipc/block_channel.h"
#include "../../common/ipc/echo_channel.h"

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
    SERVICE_REQUEST_QUEUE,
    SERVICE_BLOCK_OPEN_CHANNEL,
    SERVICE_BLOCK_CLOSE_CHANNEL,
    SERVICE_ATOMIC_MESSAGE_QUEUE_LISTEN,
    SERVICE_ATOMIC_MESSAGE_QUEUE_CONNECT,
    SERVICE_ATOMIC_MESSAGE_QUEUE_ACCEPT,
    SERVICE_ATOMIC_MESSAGE_QUEUE_CLOSE,
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
    SERVICE_EVENT_BLOCK_CHANNEL_OPEN = 8,
    SERVICE_EVENT_BLOCK_CHANNEL_CLOSED = 9,
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
    SERVICE_BLOCK_CHANNEL_NOT_FOUND = -20,
    SERVICE_BLOCK_CHANNEL_FAILED = -21,
} ServiceCode;

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

typedef struct ServiceBlockChannelOpenRequest
{
    BString device_path;
    uint64_t channel_id;
    Address channel;
    size_t channel_size;
} ServiceBlockChannelOpenRequest;

typedef ServiceBlockChannelOpenRequest * ServiceBlockChannelOpenRequestPtr;

typedef struct ServiceBlockChannelCloseRequest
{
    uint64_t channel_id;
} ServiceBlockChannelCloseRequest;

typedef ServiceBlockChannelCloseRequest * ServiceBlockChannelCloseRequestPtr;

typedef struct ServiceReadyRequest
{
    BString name;
} ServiceReadyRequest;

typedef ServiceReadyRequest * ServiceReadyRequestPtr;

typedef enum ServiceKernelEventSubscription
{
    SERVICE_KERNEL_EVENT_NONE = 0,
    SERVICE_KERNEL_EVENT_PROCESS_EXIT = 1ULL << 0,
} ServiceKernelEventSubscription;

#define SERVICE_KERNEL_EVENT_SUPPORTED_MASK SERVICE_KERNEL_EVENT_PROCESS_EXIT

typedef struct ServiceQueueRequest
{
    BString name;
    uint64_t kernel_event_subscriptions;
} ServiceQueueRequest;

typedef ServiceQueueRequest * ServiceQueueRequestPtr;

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

static inline ServiceBlockChannelOpenRequest ServiceBlockChannelOpenRequest__new(ConstBStringPtr path)
{
    return (ServiceBlockChannelOpenRequest){
        .device_path = *path,
        .channel_id = 0,
        .channel = 0,
        .channel_size = sizeof(BlockChannelMemory),
    };
}

static inline ServiceBlockChannelCloseRequest ServiceBlockChannelCloseRequest__new(uint64_t channel_id)
{
    return (ServiceBlockChannelCloseRequest){
        .channel_id = channel_id,
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

static inline ServiceQueueRequest ServiceQueueRequest__new(ConstBStringPtr name, uint64_t kernel_event_subscriptions)
{
    return (ServiceQueueRequest){
        .name = *name,
        .kernel_event_subscriptions = kernel_event_subscriptions,
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
