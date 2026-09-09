#ifndef __COMMON_INCLUDES_SYSCALLS_SOCKET_H
#define __COMMON_INCLUDES_SYSCALLS_SOCKET_H

#include <stdint.h>

#include "io.h"
#include "types/bstring.h"

#define IO_SOCKET_SCHEME "socket://"

typedef enum IOSocketStatus
{
    IO_SOCKET_OK = 0,
    IO_SOCKET_INVALID_URI = -1,
    IO_SOCKET_ENDPOINT_NOT_FOUND = -2,
    IO_SOCKET_ENDPOINT_EXISTS = -3,
    IO_SOCKET_NOT_LISTENER = -4,
    IO_SOCKET_BACKLOG_FULL = -5,
    IO_SOCKET_NO_PENDING_CONNECTION = -6,
    IO_SOCKET_ENDPOINTS_FULL = -7,
} IOSocketStatus;

typedef enum IOSocketRouterOperation
{
    IO_SOCKET_ROUTER_LISTEN = 1,
    IO_SOCKET_ROUTER_CONNECT = 2,
    IO_SOCKET_ROUTER_ACCEPT = 3,
} IOSocketRouterOperation;

typedef struct IOSocketRouterRequest
{
    uint64_t request_id;
    IOSocketRouterOperation operation;
    BString uri;
    IOHandle handle;
    uint64_t backlog;
} IOSocketRouterRequest;

typedef IOSocketRouterRequest * IOSocketRouterRequestPtr;

static inline IOSocketRouterRequest IOSocketRouterRequest__new(uint64_t request_id, IOSocketRouterOperation operation,
                                                               ConstBStringPtr uri, IOHandle handle, uint64_t backlog)
{
    return (IOSocketRouterRequest){
        .request_id = request_id,
        .operation = operation,
        .uri = *uri,
        .handle = handle,
        .backlog = backlog,
    };
}

typedef struct IOSocketRouterCompletion
{
    uint64_t request_id;
    IOSocketStatus status;
    IOHandle result;
} IOSocketRouterCompletion;

typedef IOSocketRouterCompletion * IOSocketRouterCompletionPtr;

static inline IOSocketRouterCompletion IOSocketRouterCompletion__new(uint64_t request_id, IOSocketStatus status,
                                                                     IOHandle result)
{
    return (IOSocketRouterCompletion){
        .request_id = request_id,
        .status = status,
        .result = result,
    };
}

#endif
