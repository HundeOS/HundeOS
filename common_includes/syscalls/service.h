#ifndef _COMMON_INCLUDES_SYSCALLS_SERVICE_H
#define _COMMON_INCLUDES_SYSCALLS_SERVICE_H

#include <stdint.h>

#define SERVICE_TIMER_EVENT_INTERVAL 30000ULL
#define SERVICE_TIMER_TICK_SOURCE 1ULL
#define SERVICE_DEVICE_PATH_MAX 256ULL

typedef enum ServiceOperation
{
    SERVICE_BECOME_SERVICE,
    SERVICE_WAIT,
    SERVICE_MAP_MMIO,
    SERVICE_SUBSCRIBE_IRQ,
    SERVICE_REGISTER_BLOCK_DEVICE,
    SERVICE_BLOCK_GET_REQUEST,
    SERVICE_BLOCK_COMPLETE,
    SERVICE_DEVICE_ADDRESS,
} ServiceOperation;

typedef enum ServiceEventType
{
    SERVICE_EVENT_MESSAGE = 1,
    SERVICE_EVENT_IRQ = 2,
    SERVICE_EVENT_IO = 3,
    SERVICE_EVENT_BLOCK_IO = 4,
} ServiceEventType;

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
} ServiceCode;

typedef enum ServiceBlockOperation
{
    SERVICE_BLOCK_READ = 1,
    SERVICE_BLOCK_WRITE = 2,
}
ServiceBlockOperation;

typedef struct ServiceBlockRequest
{
    uint64_t request_id;
    ServiceBlockOperation operation;
    uint64_t block;
    uint64_t block_count;
    uint64_t block_size;
    void * buffer;
    uint64_t buffer_size;
}
ServiceBlockRequest;

typedef ServiceBlockRequest * ServiceBlockRequestPtr;

typedef struct ServiceBlockCompletion
{
    uint64_t request_id;
    ServiceCode status;
    uint64_t block_count;
}
ServiceBlockCompletion;

typedef ServiceBlockCompletion * ServiceBlockCompletionPtr;

typedef struct ServiceMappedMemory ServiceMappedMemory;
typedef ServiceMappedMemory * ServiceMappedMemoryPtr;

typedef struct ServiceDeviceAddressRequest
{
    uint64_t driver_address;
    uint64_t device_address;
}
ServiceDeviceAddressRequest;

typedef ServiceDeviceAddressRequest * ServiceDeviceAddressRequestPtr;

typedef struct ServiceBlockDeviceRegistration
{
    char path[SERVICE_DEVICE_PATH_MAX];
    uint64_t block_size;
    uint64_t block_count;
}
ServiceBlockDeviceRegistration;

typedef ServiceBlockDeviceRegistration * ServiceBlockDeviceRegistrationPtr;

#endif
