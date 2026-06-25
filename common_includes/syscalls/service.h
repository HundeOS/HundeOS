#ifndef _COMMON_INCLUDES_SYSCALLS_SERVICE_H
#define _COMMON_INCLUDES_SYSCALLS_SERVICE_H

#include <stdint.h>

#define SERVICE_TIMER_EVENT_INTERVAL 30000ULL
#define SERVICE_TIMER_TICK_SOURCE 1ULL

typedef enum ServiceOperation
{
    SERVICE_WAIT,
} ServiceOperation;

typedef enum ServiceEventType
{
    SERVICE_EVENT_MESSAGE = 1,
    SERVICE_EVENT_IRQ = 2,
} ServiceEventType;

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
    SERVICE_NOT_SERVICE_PROCESS = -1,
    SERVICE_EVENT_QUEUE_EMPTY = -2,
    SERVICE_EVENT_QUEUE_FULL = -3,
    SERVICE_SCHEDULER_ERROR = -4,
    SERVICE_INVALID_BUFFER = -5,
    SERVICE_OPERATION_NOT_SUPPORTED = -6,
} ServiceCode;

#endif
