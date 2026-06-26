#ifndef _COMMON_INCLUDES_SYSCALLS_SERVICE_H
#define _COMMON_INCLUDES_SYSCALLS_SERVICE_H

#include <stdint.h>

#define SERVICE_TIMER_EVENT_INTERVAL 30000ULL
#define SERVICE_TIMER_TICK_SOURCE 1ULL

typedef enum ServiceOperation
{
    SERVICE_WAIT,
    SERVICE_MAP_MMIO,
    SERVICE_SUBSCRIBE_IRQ,
} ServiceOperation;

typedef enum ServiceEventType
{
    SERVICE_EVENT_MESSAGE = 1,
    SERVICE_EVENT_IRQ = 2,
    SERVICE_EVENT_IO = 3,
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
} ServiceCode;

typedef struct ServiceMappedRange
{
    uint64_t physical;
    uint64_t size;
    void * address;
}
ServiceMappedRange;

typedef ServiceMappedRange * ServiceMappedRangePtr;

#endif
