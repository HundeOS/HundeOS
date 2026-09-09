#include "async_io.h"
#include "../sys/mem.h"

static BString register_failed = bstr("Async IO service registration failed");
static BString service_wait_failed = bstr("Async IO service wait failed");
static BString fiber_failed = bstr("Async IO fiber operation failed");
static BString response_queue_full = bstr("Async IO response queue full");
static BString response_not_found = bstr("Async IO response not found");
static BString invalid_event_kind = bstr("Async IO invalid event kind");
static BString block_read_failed = bstr("Async IO block read failed");
static BString block_write_failed = bstr("Async IO block write failed");
static BString no_current_async_io = bstr("Async IO has no current context");

typedef struct AsyncIORequestContext
{
    AsyncIOPtr async_io;
    Anything service_context;
    ServiceEvent request;
} AsyncIORequestContext;

typedef AsyncIORequestContext * AsyncIORequestContextPtr;

ResultDef(AsyncIORequestContextPtr);
OptionalDef(AsyncIOInitService);

static Optional(AsyncIOPtr) current_async_io = None(AsyncIOPtr);

MapBodyDefine(AsyncIOResponses);

bool AsyncIOResponses__keys_equal(void * self, void * other)
{
    return *(uint64_t *)self == *(uint64_t *)other;
}

static AsyncIORequestContext AsyncIORequestContext__new(AsyncIOPtr async_io, Anything service_context,
                                                        ServiceEvent request)
{
    return (AsyncIORequestContext){
        .async_io = async_io,
        .service_context = service_context,
        .request = request,
    };
}

static Result(AsyncIORequestContextPtr)
    AsyncIORequestContext__heap_new(AsyncIOPtr async_io, Anything service_context, ServiceEvent request)
{
    AsyncIORequestContextPtr context;
    match_result(
        mem_alloc_bytes(sizeof(AsyncIORequestContext)),
        success(allocation) { context = (AsyncIORequestContextPtr)allocation; },
        error(code, message) { return Error(AsyncIORequestContextPtr, fiber_failed, code); }
    );

    *context = AsyncIORequestContext__new(async_io, service_context, request);
    return Success(AsyncIORequestContextPtr, context);
}

static void AsyncIORequestContext__dispose(AsyncIORequestContextPtr self)
{
    free(self);
}

static Result(FiberCode) AsyncIO__request_fiber_entry(FiberPtr fiber, Anything argument)
{
    AsyncIORequestContextPtr context = (AsyncIORequestContextPtr)argument;

    match_result(
        context->async_io->request_service(fiber, context->service_context, context->request),
        success(code)
        {
            AsyncIORequestContext__dispose(context);
            return Success(FiberCode, code);
        },
        error(code, message)
        {
            AsyncIORequestContext__dispose(context);
            return Error(FiberCode, message, code);
        }
    );

    return Error(FiberCode, fiber_failed, FIBER_INVALID_STATE);
}

AsyncIO AsyncIO__new(Anything service_context, AsyncIORequestService request_service)
{
    return AsyncIO__new_with_max_fibers(service_context, request_service, ASYNC_IO_DEFAULT_MAX_FIBERS);
}

AsyncIO AsyncIO__new_with_max_fibers(Anything service_context, AsyncIORequestService request_service, size_t max_fibers)
{
    return (AsyncIO){
        .scheduler = FiberScheduler__new_with_max_fibers(max_fibers),
        .responses = AsyncIOResponses__new(),
        .service_context = service_context,
        .request_service = request_service,
    };
}

Result(AsyncIOPtr) AsyncIO__current(void)
{
    match_optional(
        current_async_io,
        some(async_io) { return Success(AsyncIOPtr, async_io); },
        none() { return Error(AsyncIOPtr, no_current_async_io, ASYNC_IO_WAIT_FAILED); }
    );

    return Error(AsyncIOPtr, no_current_async_io, ASYNC_IO_WAIT_FAILED);
}

Result(AsyncIOCode) AsyncIO__start(AsyncIOPtr self, AsyncIORegisterService register_service, Anything service_context,
                                   AsyncIORequestService request_service)
{
    return AsyncIO__start_with_max_fibers(self, register_service, service_context, request_service,
                                          ASYNC_IO_DEFAULT_MAX_FIBERS);
}

Result(AsyncIOCode)
    AsyncIO__start_with_init(AsyncIOPtr self, AsyncIORegisterService register_service, Anything service_context,
                             AsyncIOInitService init_service, AsyncIORequestService request_service)
{
    *self = AsyncIO__new(service_context, request_service);
    current_async_io = Some(AsyncIOPtr, self);

    match_result(
        register_service(service_context),
        success_do_nothing(),
        error(code, message) { return Error(AsyncIOCode, message, ASYNC_IO_REGISTER_FAILED); }
    );

    match_result(
        FiberScheduler__spawn(&self->scheduler, init_service, service_context),
        success_do_nothing(),
        error(code, message) { return Error(AsyncIOCode, message, ASYNC_IO_FIBER_FAILED); }
    );

    match_result(
        FiberScheduler__run_ready_until_blocked(&self->scheduler),
        success_do_nothing(),
        error(code, message) { return Error(AsyncIOCode, message, ASYNC_IO_FIBER_FAILED); }
    );

    while (true)
    {
        ServiceEvent event = ServiceEvent__request(SERVICE_EVENT_MESSAGE, 0, 0);
        ServiceCode wait_status = service_wait(&event);
        if (wait_status == SERVICE_COMPLETE)
        {
            return Success(AsyncIOCode, ASYNC_IO_WAIT_COMPLETE);
        }
        if (!service_ok(wait_status))
        {
            return Error(AsyncIOCode, service_wait_failed, ASYNC_IO_WAIT_FAILED);
        }

        match_result(
            AsyncIO__handle_event(self, &event),
            success_do_nothing(),
            error(code, message) { return Error(AsyncIOCode, message, code); }
        );

        match_result(
            FiberScheduler__run_ready_until_blocked(&self->scheduler),
            success_do_nothing(),
            error(code, message) { return Error(AsyncIOCode, message, ASYNC_IO_FIBER_FAILED); }
        );
    }
}

Result(AsyncIOCode)
    AsyncIO__start_with_max_fibers(AsyncIOPtr self, AsyncIORegisterService register_service, Anything service_context,
                                   AsyncIORequestService request_service, size_t max_fibers)
{
    *self = AsyncIO__new_with_max_fibers(service_context, request_service, max_fibers);
    current_async_io = Some(AsyncIOPtr, self);

    match_result(
        register_service(service_context),
        success_do_nothing(),
        error(code, message) { return Error(AsyncIOCode, message, ASYNC_IO_REGISTER_FAILED); }
    );

    while (true)
    {
        ServiceEvent event = ServiceEvent__request(SERVICE_EVENT_MESSAGE, 0, 0);
        ServiceCode wait_status = service_wait(&event);
        if (wait_status == SERVICE_COMPLETE)
        {
            return Success(AsyncIOCode, ASYNC_IO_WAIT_COMPLETE);
        }
        if (!service_ok(wait_status))
        {
            return Error(AsyncIOCode, service_wait_failed, ASYNC_IO_WAIT_FAILED);
        }

        match_result(
            AsyncIO__handle_event(self, &event),
            success_do_nothing(),
            error(code, message) { return Error(AsyncIOCode, message, code); }
        );

        match_result(
            FiberScheduler__run_ready_until_blocked(&self->scheduler),
            success_do_nothing(),
            error(code, message) { return Error(AsyncIOCode, message, ASYNC_IO_FIBER_FAILED); }
        );
    }
}

Result(AsyncIOCode) AsyncIO__handle_event(AsyncIOPtr self, ServiceEventPtr event)
{
    current_async_io = Some(AsyncIOPtr, self);

    switch (ServiceEvent__kind(event))
    {
    case SERVICE_EVENT_REQUEST:
        return AsyncIO__spawn_request(self, event);

    case SERVICE_EVENT_RESPONSE:
        return AsyncIO__deliver_response(self, event);
    }

    return Error(AsyncIOCode, invalid_event_kind, ASYNC_IO_INVALID_EVENT_KIND);
}

Result(AsyncIOCode) AsyncIO__spawn_request(AsyncIOPtr self, ServiceEventPtr event)
{
    current_async_io = Some(AsyncIOPtr, self);

    AsyncIORequestContextPtr context;
    match_result(
        AsyncIORequestContext__heap_new(self, self->service_context, *event),
        success(created_context) { context = created_context; },
        error(code, message) { return Error(AsyncIOCode, message, ASYNC_IO_FIBER_FAILED); }
    );

    match_result(
        FiberScheduler__spawn(&self->scheduler, AsyncIO__request_fiber_entry, (Anything)context),
        success_do_nothing(),
        error(code, message)
        {
            AsyncIORequestContext__dispose(context);
            return Error(AsyncIOCode, message, ASYNC_IO_FIBER_FAILED);
        }
    );

    return Success(AsyncIOCode, ASYNC_IO_OK);
}

Result(AsyncIOCode) AsyncIO__deliver_response(AsyncIOPtr self, ServiceEventPtr event)
{
    current_async_io = Some(AsyncIOPtr, self);

    uint64_t request_id = ServiceEvent__request_id(event);
    match_result(
        AsyncIOResponses__set(&self->responses, request_id, *event),
        success_do_nothing(),
        error(code, message) { return Error(AsyncIOCode, response_queue_full, ASYNC_IO_RESPONSE_QUEUE_FULL); }
    );

    FiberWaitKey key = FiberWaitKey__request(request_id);
    FiberWakeValue value = FiberWakeValue__new(&key, FIBER_OK, request_id);
    match_result(
        FiberScheduler__wake(&self->scheduler, key, value),
        success(_) { return Success(AsyncIOCode, ASYNC_IO_OK); },
        error(code, message)
        {
            if (code == FIBER_WAIT_NOT_FOUND)
            {
                return Success(AsyncIOCode, ASYNC_IO_OK);
            }

            return Error(AsyncIOCode, message, ASYNC_IO_FIBER_FAILED);
        }
    );

    return Success(AsyncIOCode, ASYNC_IO_OK);
}

Result(ServiceEvent) AsyncIO__take_response(AsyncIOPtr self, uint64_t request_id)
{
    match_optional(
        AsyncIOResponses__get(&self->responses, request_id),
        some(response)
        {
            match_result(
                AsyncIOResponses__delete(&self->responses, request_id),
                success_do_nothing(),
                error(code, message) { return Error(ServiceEvent, message, ASYNC_IO_RESPONSE_NOT_FOUND); }
            );

            return Success(ServiceEvent, response);
        },
        none() { return Error(ServiceEvent, response_not_found, ASYNC_IO_RESPONSE_NOT_FOUND); }
    );

    return Error(ServiceEvent, response_not_found, ASYNC_IO_RESPONSE_NOT_FOUND);
}

Result(ServiceEvent) AsyncIO__yield_for_response(uint64_t request_id)
{
    AsyncIOPtr async_io;
    match_result(
        AsyncIO__current(),
        success(current) { async_io = current; },
        error(code, message) { return Error(ServiceEvent, message, code); }
    );

    match_result(
        AsyncIO__take_response(async_io, request_id),
        success(response) { return Success(ServiceEvent, response); },
        error(code, message)
        {
            if (code != ASYNC_IO_RESPONSE_NOT_FOUND)
            {
                return Error(ServiceEvent, message, code);
            }
        }
    );

    match_result(
        FiberScheduler__await_current(&async_io->scheduler, FiberWaitKey__request(request_id)),
        success_do_nothing(),
        error(code, message) { return Error(ServiceEvent, message, code); }
    );

    return AsyncIO__take_response(async_io, request_id);
}

static Result(ServiceBlockReadResult)
    IO__block_result(ServiceBlockReadResult request_id, uint64_t expected_blocks, ConstBStringPtr error_message)
{
    if (request_id <= 0)
    {
        return Error(ServiceBlockReadResult, *error_message, request_id);
    }

    match_result(
        AsyncIO__yield_for_response((uint64_t)request_id),
        success(response)
        {
            if (response.type != SERVICE_EVENT_BLOCK_IO || response.data == 0 || response.data > expected_blocks)
            {
                return Error(ServiceBlockReadResult, *error_message, SERVICE_BLOCK_REQUEST_FAILED);
            }

            return Success(ServiceBlockReadResult, (ServiceBlockReadResult)response.data);
        },
        error(code, message) { return Error(ServiceBlockReadResult, message, code); }
    );

    return Error(ServiceBlockReadResult, *error_message, SERVICE_BLOCK_REQUEST_FAILED);
}

Result(ServiceBlockReadResult)
    IO__block_read(ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size)
{
    ServiceBlockReadResult request_id = service_block_read_device_async(path, block, block_count, buffer, buffer_size);
    return IO__block_result(request_id, block_count, &block_read_failed);
}

Result(ServiceBlockReadResult)
    IO__block_write(ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size)
{
    ServiceBlockReadResult request_id = service_block_write_device_async(path, block, block_count, buffer, buffer_size);
    return IO__block_result(request_id, block_count, &block_write_failed);
}

size_t AsyncIO__pending_response_count(AsyncIOPtr self)
{
    return AsyncIOResponses__len(&self->responses);
}

size_t AsyncIO__live_fiber_count(AsyncIOPtr self)
{
    return FiberScheduler__live_count(&self->scheduler);
}

size_t AsyncIO__max_fibers(AsyncIOPtr self)
{
    return FiberScheduler__max_fibers(&self->scheduler);
}
