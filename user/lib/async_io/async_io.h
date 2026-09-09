#ifndef __USER_LIB_ASYNC_IO_ASYNC_IO_H
#define __USER_LIB_ASYNC_IO_ASYNC_IO_H

#include "../fiber/scheduler.h"
#include "../sys/service.h"
#include "../types/map.h"

#define ASYNC_IO_MAX_PENDING_RESPONSES 64
#define ASYNC_IO_DEFAULT_MAX_FIBERS FIBER_SCHEDULER_DEFAULT_MAX_FIBERS

typedef struct AsyncIO AsyncIO;
typedef AsyncIO * AsyncIOPtr;

typedef enum AsyncIOCode
{
    ASYNC_IO_OK = 0,
    ASYNC_IO_WAIT_COMPLETE = 1,
    ASYNC_IO_WAITING = 2,
    ASYNC_IO_REGISTER_FAILED = -1,
    ASYNC_IO_WAIT_FAILED = -2,
    ASYNC_IO_FIBER_FAILED = -3,
    ASYNC_IO_RESPONSE_QUEUE_FULL = -4,
    ASYNC_IO_RESPONSE_NOT_FOUND = -5,
    ASYNC_IO_INVALID_EVENT_KIND = -6,
} AsyncIOCode;

ResultDef(AsyncIOCode);
ResultDef(ServiceBlockReadResult);
ResultDef(ServiceEvent);
ResultDef(AsyncIOPtr);
OptionalDef(ServiceEvent);
OptionalDef(AsyncIOPtr);

typedef Result(AsyncIOCode) (*AsyncIORegisterService)(Anything service_context);
typedef Result(FiberCode) (*AsyncIORequestService)(FiberPtr fiber, Anything service_context, ServiceEvent request);
typedef Result(FiberCode) (*AsyncIOInitService)(FiberPtr fiber, Anything service_context);

bool AsyncIOResponses__keys_equal(void * self, void * other);
MapDefine(uint64_t, ServiceEvent, ASYNC_IO_MAX_PENDING_RESPONSES, AsyncIOResponses, AsyncIOResponses__keys_equal);

struct AsyncIO
{
    FiberScheduler scheduler;
    AsyncIOResponses responses;
    Anything service_context;
    AsyncIORequestService request_service;
};

AsyncIO AsyncIO__new(Anything service_context, AsyncIORequestService request_service);
AsyncIO AsyncIO__new_with_max_fibers(Anything service_context, AsyncIORequestService request_service,
                                     size_t max_fibers);
Result(AsyncIOCode) AsyncIO__start(AsyncIOPtr self, AsyncIORegisterService register_service, Anything service_context,
                                   AsyncIORequestService request_service);
Result(AsyncIOCode)
    AsyncIO__start_with_init(AsyncIOPtr self, AsyncIORegisterService register_service, Anything service_context,
                             AsyncIOInitService init_service, AsyncIORequestService request_service);
Result(AsyncIOCode)
    AsyncIO__start_with_max_fibers(AsyncIOPtr self, AsyncIORegisterService register_service, Anything service_context,
                                   AsyncIORequestService request_service, size_t max_fibers);
Result(AsyncIOCode) AsyncIO__handle_event(AsyncIOPtr self, ServiceEventPtr event);
Result(AsyncIOCode) AsyncIO__spawn_request(AsyncIOPtr self, ServiceEventPtr event);
Result(AsyncIOCode) AsyncIO__deliver_response(AsyncIOPtr self, ServiceEventPtr event);
Result(ServiceEvent) AsyncIO__take_response(AsyncIOPtr self, uint64_t request_id);
Result(ServiceEvent) AsyncIO__yield_for_response(uint64_t request_id);
Result(ServiceBlockReadResult)
    IO__block_read(ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size);
Result(ServiceBlockReadResult)
    IO__block_write(ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size);
Result(AsyncIOPtr) AsyncIO__current(void);
size_t AsyncIO__pending_response_count(AsyncIOPtr self);
size_t AsyncIO__live_fiber_count(AsyncIOPtr self);
size_t AsyncIO__max_fibers(AsyncIOPtr self);

#endif
