#include "socket.h"

#include "../../../kalloc.h"
#include "../../../kernel_core.h"
#include "../../../mutex.h"
#include "../../../scheduler/parked.h"
#include "../../../scheduler/scheduler.h"
#include "../../../subsystems/io/block/block_device.h"
#include "../../../subsystems/io/io_subsystem.h"
#include "../../../subsystems/message/io_operation_message.h"
#include "../../../subsystems/message/subsystem_codes.h"
#include "../../../types/map.h"
#include "../../../types/fixed_array.h"

#define MAX_SOCKET_REQUESTS IO_MAX_DEVICES

typedef struct SocketRequestState
{
    IOSocketRouterRequest request;
    KernelContextPtr kernel_context;
    Optional(ProcessID) waiting_pid;
    SocketRequestComplete complete;
    Anything complete_context;
    bool parked_operation_owns_context;
    bool completed;
    IOSocketStatus status;
    IOHandle result;
}
SocketRequestState;

MapBodyDefine(SocketRequestMap);

static BString socket_router_not_registered = bstr("Socket router is not registered");
static BString socket_request_not_found = bstr("Socket request not found");
static BString socket_request_failed = bstr("Socket request failed");
static BString socket_request_pending = bstr("Socket request pending");
static BString socket_request_queue_full = bstr("Socket request queue full");
static BString socket_allocation_failed = bstr("Socket request allocation failed");
static BString socket_scheduler_failed = bstr("Socket scheduler operation failed");
static BString socket_kernel_not_found = bstr("Socket kernel not found");
static BString socket_device_not_registered = bstr("Socket IO is not registered");

static SocketRequestState SocketRequestState__new(
    IOSocketRouterRequest request,
    KernelContext kernel_context,
    ProcessID waiting_pid,
    SocketRequestComplete complete,
    Anything complete_context
)
{
    return (SocketRequestState) {
        .request = request,
        .kernel_context = ptr(Kernel__alloc_kernel(sizeof(KernelContext)), KernelContext),
        .waiting_pid = Some(ProcessID, waiting_pid),
        .complete = complete,
        .complete_context = complete_context,
        .parked_operation_owns_context = false,
        .completed = false,
        .status = IO_SOCKET_ENDPOINT_NOT_FOUND,
        .result = -1,
    };
}

static Result(SocketRequestStatePtr) SocketRequestState__heap_new(
    IOSocketRouterRequest request,
    KernelContext kernel_context,
    ProcessID waiting_pid,
    SocketRequestComplete complete,
    Anything complete_context
)
{
    KernelAddress address = Kernel__alloc(sizeof(SocketRequestState));
    if (!address.valid)
    {
        return Error(SocketRequestStatePtr, socket_allocation_failed, SERVICE_SOCKET_REQUEST_FAILED);
    }

    SocketRequestStatePtr self = ptr(address.virtual_addr, SocketRequestState);
    *self = SocketRequestState__new(request, kernel_context, waiting_pid, complete, complete_context);
    *self->kernel_context = kernel_context;
    return Success(SocketRequestStatePtr, self);
}

static void SocketRequestState__dispose(SocketRequestStatePtr self)
{
    if (!self->parked_operation_owns_context)
    {
        KernelContext__dispose_heap(self->kernel_context);
    }
    Kernel__free(addr(self));
}

static Result(ServiceCode) SocketRegistry__deliver_service_event(
    SocketRegistryPtr self,
    ProcessPtr router_process,
    ServiceEvent event
)
{
    match_optional(MessageBrokerPtr, self->message_broker,
        some(message_broker)
        {
            MessagePtr message = Message__heap_new_service_event_delivery(
                SOURCE_SERVICE, SUBSYS_SERVICE_EVENT_AVAILABLE, router_process, event
            );

            match_result(AckCode, MessageBroker__deliver(message_broker, message),
                success(_) { return Success(ServiceCode, SERVICE_OK); },
                error(code, message) { return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR); }
            );
        },
        none_do_nothing()
    );

    return Error(ServiceCode, socket_router_not_registered, SERVICE_SOCKET_REQUEST_FAILED);
}

static Result(Anything) SocketRegistry__complete_wait(ParkedOperationPtr parked_operation)
{
    Optional(SocketRequestStatePtr) completed_request = None(SocketRequestStatePtr);
    Optional(SocketRegistryPtr) registry_opt = None(SocketRegistryPtr);
    uint64_t request_id = 0;
    Result(Anything) wait_result = Error(Anything, socket_request_not_found, IO_INVALID_PATH);

    match_optional(SchedulerPtr, parked_operation->kernel_context->scheduler,
        some(scheduler)
        {
            match_optional(KernelPtr, scheduler->kernel,
                some(kernel)
                {
                    match_result(SocketRegistryPtr, SocketIO__registered_registry(kernel->io_subsystem),
                        success(registry) { registry_opt = Some(SocketRegistryPtr, registry); },
                        error(code, message) { registry_opt = None(SocketRegistryPtr); }
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    SocketRegistryPtr registry;
    match_optional(SocketRegistryPtr, registry_opt,
        some(found) { registry = found; },
        none() { return Error(Anything, socket_kernel_not_found, IO_INVALID_PATH); }
    );

    match_optional(OperationKey, parked_operation->operation_key,
        some(key) { request_id = key; },
        none() { return Error(Anything, socket_request_not_found, IO_INVALID_PATH); }
    );

    critical_section(registry->lock,
        match_optional(SocketRequestStatePtr, SocketRequestMap__get(&registry->requests, request_id),
            some(request)
            {
                if (!request->completed)
                {
                    wait_result = Error(Anything, socket_request_pending, IO_WAIT_FOR_DATA);
                }
                else
                {
                    match_result(CollectionCode, SocketRequestMap__delete(&registry->requests, request_id),
                        success_do_nothing(),
                        error(code, message)
                        {
                            __d_printf(IODEV, "socket request delete failed id=%i code=%i\n", (int) request_id, (int) code);
                        }
                    );
                    completed_request = Some(SocketRequestStatePtr, request);
                    IOSocketRouterCompletion completion =
                        IOSocketRouterCompletion__new(request->request.request_id, request->status, request->result);
                    match_result(Anything, request->complete(
                        &request->request, &completion, request->complete_context
                    ),
                        success(value)
                        {
                            Result(Anything) mapped_result = Success(Anything, value);
                            wait_result = KernelContext__map_result(
                                request->kernel_context,
                                &mapped_result
                            );
                        },
                        error(code, message)
                        {
                            wait_result = KernelContext__map_result(
                                request->kernel_context,
                                &Error(Anything, message, code)
                            );
                        }
                    );
                }
            },
            none()
            {
                wait_result = Error(Anything, socket_request_not_found, IO_INVALID_PATH);
            }
        );
    );

    match_result(Anything, wait_result,
        success(value)
        {
            match_optional(SocketRequestStatePtr, completed_request,
                some(request) { SocketRequestState__dispose(request); },
                none_do_nothing()
            );
            return Success(Anything, value);
        },
        error(code, message)
        {
            if (code != IO_WAIT_FOR_DATA && code != IO_INVALID_PATH)
            {
                match_optional(SocketRequestStatePtr, completed_request,
                    some(request) { SocketRequestState__dispose(request); },
                    none_do_nothing()
                );
            }
            return Error(Anything, message, code);
        }
    );
}

Result(ServiceCode) SocketRegistry__register_router(
    SocketRegistryPtr self,
    ProcessPtr service_process,
    MessageBrokerPtr message_broker
)
{
    critical_section(self->lock,
        self->router_process = Some(ProcessPtr, service_process);
        self->message_broker = Some(MessageBrokerPtr, message_broker);
    );

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) SocketRegistry__start_request(
    SocketRegistryPtr self,
    IOSocketRouterRequest request,
    KernelContext kernel_context,
    SocketRequestComplete complete,
    Anything complete_context
)
{
    Optional(ProcessPtr) router_process = None(ProcessPtr);
    Optional(MessageBrokerPtr) message_broker = None(MessageBrokerPtr);
    Result(SocketRequestStatePtr) request_state_result =
        Error(SocketRequestStatePtr, socket_router_not_registered, SERVICE_SOCKET_REQUEST_FAILED);

    match_optional(ProcessID, kernel_context.pid,
        some(pid)
        {
            critical_section(self->lock,
                match_optional(ProcessPtr, self->router_process,
                    some(router)
                    {
                        request.request_id = self->next_request_id++;
                        router_process = Some(ProcessPtr, router);
                        message_broker = self->message_broker;
                    },
                    none_do_nothing()
                );
            );
            match_optional(ProcessPtr, router_process,
                some(router)
                {
                    request_state_result =
                        SocketRequestState__heap_new(request, kernel_context, pid, complete, complete_context);
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    SocketRequestStatePtr request_state;
    match_result(SocketRequestStatePtr, request_state_result,
        success(created_request) { request_state = created_request; },
        error(code, message) { return Error(ServiceCode, message, code); }
    );

    Result(CollectionCode) set_result = Error(CollectionCode, socket_request_queue_full, MAP_FULL);
    critical_section(self->lock,
        set_result = SocketRequestMap__set(&self->requests, request_state->request.request_id, request_state);
    );

    match_result(CollectionCode, set_result,
        success_do_nothing(),
        error(code, message)
        {
            SocketRequestState__dispose(request_state);
            return Error(ServiceCode, socket_request_queue_full, SERVICE_SOCKET_REQUEST_FAILED);
        }
    );

    match_optional(ProcessPtr, router_process,
        some(router)
        {
            ServiceEvent event = ServiceEvent__request(
                SERVICE_EVENT_SOCKET_IO,
                request_state->request.request_id,
                0
            );
            match_result(ServiceCode, SocketRegistry__deliver_service_event(self, router, event),
                success_do_nothing(),
                error(code, message)
                {
                    SocketRequestMap__delete(&self->requests, request_state->request.request_id);
                    SocketRequestState__dispose(request_state);
                    return Error(ServiceCode, message, code);
                }
            );
        },
        none_do_nothing()
    );

    match_optional(ProcessID, request_state->waiting_pid,
        some(pid)
        {
            match_optional(MessageBrokerPtr, message_broker,
                some(broker)
                {
                    MessagePtr message = Message__heap_new_deferred_with_context_ownership_and_operation_key(
                        SOURCE_IO,
                        SUBSYS_IO_PARK,
                        pid,
                        request_state->kernel_context,
                        SocketRegistry__complete_wait,
                        true,
                        &Some(OperationKey, request_state->request.request_id)
                    );

                    match_result(AckCode, MessageBroker__deliver(broker, message),
                        success(_)
                        {
                            request_state->parked_operation_owns_context = true;
                            return Success(ServiceCode, SERVICE_OK);
                        },
                        error(code, message)
                        {
                            SocketRequestMap__delete(&self->requests, request_state->request.request_id);
                            SocketRequestState__dispose(request_state);
                            return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR);
                        }
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    return Error(ServiceCode, socket_scheduler_failed, SERVICE_SCHEDULER_ERROR);
}

Result(IOSocketRouterRequest) SocketRegistry__get_request(
    SocketRegistryPtr self,
    ProcessPtr service_process,
    uint64_t request_id
)
{
    Result(IOSocketRouterRequest) result =
        Error(IOSocketRouterRequest, socket_request_not_found, SERVICE_SOCKET_REQUEST_NOT_FOUND);

    critical_section(self->lock,
        match_optional(SocketRequestStatePtr, SocketRequestMap__get(&self->requests, request_id),
            some(request)
            {
                match_optional(ProcessPtr, self->router_process,
                    some(router)
                    {
                        if (router == service_process)
                        {
                            result = Success(IOSocketRouterRequest, request->request);
                        }
                    },
                    none_do_nothing()
                );
            },
            none_do_nothing()
        );
    );

    return result;
}

Result(ServiceCode) SocketRegistry__complete(
    SocketRegistryPtr self,
    ProcessPtr service_process,
    IOSocketRouterCompletionPtr completion
)
{
    Optional(ProcessID) waiting_pid = None(ProcessID);
    Optional(MessageBrokerPtr) message_broker = None(MessageBrokerPtr);
    Result(ServiceCode) result = Error(ServiceCode, socket_request_not_found, SERVICE_SOCKET_REQUEST_NOT_FOUND);

    critical_section(self->lock,
        match_optional(SocketRequestStatePtr, SocketRequestMap__get(&self->requests, completion->request_id),
            some(request)
            {
                match_optional(ProcessPtr, self->router_process,
                    some(router)
                    {
                        if (router == service_process)
                        {
                            request->completed = true;
                            request->status = completion->status;
                            request->result = completion->result;
                            waiting_pid = request->waiting_pid;
                            message_broker = self->message_broker;
                            result = Success(ServiceCode, SERVICE_OK);
                        }
                    },
                    none_do_nothing()
                );
            },
            none_do_nothing()
        );
    );

    match_result(ServiceCode, result,
        success_do_nothing(),
        error(code, message) { return Error(ServiceCode, message, code); }
    );

    match_optional(ProcessID, waiting_pid,
        some(pid)
        {
            match_optional(MessageBrokerPtr, message_broker,
                some(broker)
                {
                    MessagePtr message = Message__heap_new_io_operation(
                        SOURCE_IO, SUBSYS_IO_OPERATION_AVAILABLE, pid, completion->request_id
                    );
                    match_result(AckCode, MessageBroker__deliver(broker, message),
                        success(_) { return Success(ServiceCode, SERVICE_OK); },
                        error(code, message) { return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR); }
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    return Error(ServiceCode, socket_scheduler_failed, SERVICE_SCHEDULER_ERROR);
}

void SocketRegistry__reset(SocketRegistryPtr self)
{
    critical_section(self->lock,
        for_each_map(SocketRequestMap, self->requests, index, request_id, request,
        {
            SocketRequestState__dispose(request);
        });
        self->router_process = None(ProcessPtr);
        self->message_broker = None(MessageBrokerPtr);
        self->requests = SocketRequestMap__new();
        self->next_request_id = 1;
    );
}

typedef enum SocketHandleKind
{
    SOCKET_HANDLE_LISTENER = 1,
    SOCKET_HANDLE_CONNECTION = 2,
}
SocketHandleKind;

FixedArrayDefine(IOInternalHandle, 64, SocketPendingConnections, SimpleEquals(uint32_t));
FixedArrayBodyDefine(SocketPendingConnections);

typedef struct SocketHandle
{
    IOInternalHandle handle;
    SocketHandleKind kind;
    Optional(IOInternalHandle) peer;
    IOBufferPtr buffer;
    Optional(IOContextPtr) parked_reader;
    SocketPendingConnections pending_connections;
}
SocketHandle;

typedef SocketHandle * SocketHandlePtr;

OptionalDef(SocketHandlePtr);
ResultDef(SocketHandlePtr);

MapDefine(IOInternalHandle, SocketHandlePtr, MAX_HANDLES_PER_DEVICE, SocketHandleMap, SimpleEquals(uint32_t));
MapBodyDefine(SocketHandleMap);

struct SocketIOState
{
    IOState state;
    SocketHandleMap handles;
    IOInternalHandle next_handle;
    MessageBrokerPtr message_broker;
    SocketIOPtr socket_io;
    mutex_t lock;
};

static BString socket_invalid_operation = bstr("Invalid socket operation");
static BString socket_invalid_handle = bstr("Invalid socket handle");
static BString socket_not_connected = bstr("Socket is not connected");
static BString socket_accept_queue_empty = bstr("Socket accept queue empty");

static SocketHandle SocketHandle__new(IOInternalHandle handle, SocketHandleKind kind)
{
    return (SocketHandle) {
        .handle = handle,
        .kind = kind,
        .peer = None(IOInternalHandle),
        .buffer = IOBuffer__heap_new(),
        .parked_reader = None(IOContextPtr),
        .pending_connections = SocketPendingConnections__new(),
    };
}

static Result(SocketHandlePtr) SocketHandle__heap_new(IOInternalHandle handle, SocketHandleKind kind)
{
    KernelAddress address = Kernel__alloc(sizeof(SocketHandle));
    if (!address.valid)
    {
        return Error(SocketHandlePtr, socket_allocation_failed, IO_REGISTER_FAILED);
    }

    SocketHandlePtr self = ptr(address.virtual_addr, SocketHandle);
    *self = SocketHandle__new(handle, kind);
    return Success(SocketHandlePtr, self);
}

static void SocketHandle__dispose(SocketHandlePtr self)
{
    Kernel__free(addr(self->buffer));
    Kernel__free(addr(self));
}

static SocketIOState SocketIOState__new(MessageBrokerPtr message_broker, SocketIOPtr socket_io)
{
    return (SocketIOState) {
        .state = IOState__new(),
        .handles = SocketHandleMap__new(),
        .next_handle = 1,
        .message_broker = message_broker,
        .socket_io = socket_io,
        .lock = MUTEX_INIT,
    };
}

SocketIOStatePtr SocketIOState__heap_new(MessageBrokerPtr message_broker, SocketIOPtr socket_io)
{
    SocketIOStatePtr self = ptr(Kernel__alloc_kernel(sizeof(SocketIOState)), SocketIOState);
    *self = SocketIOState__new(message_broker, socket_io);
    return self;
}

static Result(SocketHandlePtr) SocketIOState__add_handle(SocketIOStatePtr self, SocketHandleKind kind)
{
    IOInternalHandle handle = 0;
    critical_section(self->lock,
        handle = self->next_handle++;
    );

    SocketHandlePtr socket_handle;
    match_result(SocketHandlePtr, SocketHandle__heap_new(handle, kind),
        success(created) { socket_handle = created; },
        error(code, message) { return Error(SocketHandlePtr, message, code); }
    );

    critical_section(self->lock,
        match_result(CollectionCode, SocketHandleMap__set(&self->handles, handle, socket_handle),
            success_do_nothing(),
            error(code, message)
            {
                SocketHandle__dispose(socket_handle);
                cs_return Error(SocketHandlePtr, message, IO_REGISTER_FAILED);
            }
        );
    );

    return Success(SocketHandlePtr, socket_handle);
}

static Result(SocketHandlePtr) SocketIOState__get_handle(SocketIOStatePtr self, IOInternalHandle handle)
{
    Optional(SocketHandlePtr) found = None(SocketHandlePtr);
    critical_section(self->lock,
        found = SocketHandleMap__get(&self->handles, handle);
    );

    match_optional(SocketHandlePtr, found,
        some(socket_handle) { return Success(SocketHandlePtr, socket_handle); },
        none() { return Error(SocketHandlePtr, socket_invalid_handle, IO_UNKNOWN_HANDLE); }
    );
}

static Result(Anything) SocketIO__complete_listen(
    IOSocketRouterRequestPtr request,
    IOSocketRouterCompletionPtr completion,
    Anything context
)
{
    if (completion->status != IO_SOCKET_OK)
    {
        return Error(Anything, socket_request_failed, completion->status);
    }

    return Success(Anything, request->handle);
}

static Result(Anything) SocketIO__complete_connect(
    IOSocketRouterRequestPtr request,
    IOSocketRouterCompletionPtr completion,
    Anything context
)
{
    SocketIOStatePtr self = (SocketIOStatePtr) context;
    if (completion->status != IO_SOCKET_OK)
    {
        return Error(Anything, socket_request_failed, completion->status);
    }

    IOInternalHandle client_handle = (IOInternalHandle) request->handle;
    IOInternalHandle listener_handle = (IOInternalHandle) completion->result;

    SocketHandlePtr listener;
    SocketHandlePtr client;
    SocketHandlePtr server;
    match_result(SocketHandlePtr, SocketIOState__get_handle(self, listener_handle),
        success(found) { listener = found; },
        error(code, message) { return Error(Anything, message, code); }
    );
    match_result(SocketHandlePtr, SocketIOState__get_handle(self, client_handle),
        success(found) { client = found; },
        error(code, message) { return Error(Anything, message, code); }
    );
    match_result(SocketHandlePtr, SocketIOState__add_handle(self, SOCKET_HANDLE_CONNECTION),
        success(created) { server = created; },
        error(code, message) { return Error(Anything, message, code); }
    );

    Optional(IOContextPtr) parked_accept = None(IOContextPtr);
    critical_section(self->lock,
        client->peer = Some(IOInternalHandle, server->handle);
        server->peer = Some(IOInternalHandle, client->handle);
        match_result(CollectionCode, SocketPendingConnections__push(
            &listener->pending_connections, server->handle
        ),
            success_do_nothing(),
            error(code, message) { cs_return Error(Anything, message, IO_REGISTER_FAILED); }
        );
        parked_accept = listener->parked_reader;
        listener->parked_reader = None(IOContextPtr);
    );

    match_optional(IOContextPtr, parked_accept,
        some(parked_context)
        {
            match_optional(ProcessID, parked_context->kernel_context.pid,
                some(pid)
                {
                    match_optional(IOHandle, parked_context->handle,
                        some(context_handle)
                        {
                            MessagePtr message = Message__heap_new_io_operation(
                                SOURCE_IO, SUBSYS_IO_OPERATION_AVAILABLE, pid, context_handle
                            );
                            match_result(AckCode, MessageBroker__deliver(self->message_broker, message),
                                success_do_nothing(),
                                error(code, message) { return Error(Anything, message, code); }
                            );
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    return Success(Anything, client_handle);
}

static Result(ServiceCode) SocketIO__start_router_request(
    SocketIOStatePtr self,
    IOSocketRouterOperation operation,
    ConstBStringPtr path,
    IOInternalHandle handle,
    Flags flags
)
{
    match_optional(KernelContextPtr, BlockDevice__current_context(),
        some(kernel_context)
        {
            uint64_t backlog = 0;
            if ((flags & IO_OPEN_LISTEN) != 0)
            {
                backlog = 64;
            }
            SocketRequestComplete complete = SocketIO__complete_connect;
            if (operation == IO_SOCKET_ROUTER_LISTEN)
            {
                complete = SocketIO__complete_listen;
            }

            IOSocketRouterRequest request = IOSocketRouterRequest__new(0, operation, path, handle, backlog);
            return SocketRegistry__start_request(
                &self->socket_io->registry, request, *kernel_context, complete, (Anything) self
            );
        },
        none_do_nothing()
    );

    return Error(ServiceCode, socket_router_not_registered, SERVICE_SOCKET_REQUEST_FAILED);
}

static Result(IOInternalHandle) SocketIO__open(IOStatePtr state, ConstBStringPtr path, Flags flags)
{
    SocketIOStatePtr self = (SocketIOStatePtr) state;
    IOSocketRouterOperation operation = IO_SOCKET_ROUTER_CONNECT;
    SocketHandleKind handle_kind = SOCKET_HANDLE_CONNECTION;
    if ((flags & IO_OPEN_BIND) != 0 || (flags & IO_OPEN_LISTEN) != 0)
    {
        operation = IO_SOCKET_ROUTER_LISTEN;
        handle_kind = SOCKET_HANDLE_LISTENER;
    }

    match_result(SocketHandlePtr, SocketIOState__add_handle(self, handle_kind),
        success(socket_handle)
        {
            match_result(ServiceCode, SocketIO__start_router_request(
                self, operation, path, socket_handle->handle, flags
            ),
                success(_) { return Error(IOInternalHandle, socket_request_pending, IO_WAIT_FOR_DATA); },
                error(code, message)
                {
                    SocketHandleMap__delete(&self->handles, socket_handle->handle);
                    SocketHandle__dispose(socket_handle);
                    return Error(IOInternalHandle, message, IO_INVALID_PATH);
                }
            );
        },
        error(code, message) { return Error(IOInternalHandle, message, code); }
    );

    return Error(IOInternalHandle, socket_invalid_operation, IO_INVALID_PATH);
}

static Result(IOInternalHandle) SocketIO__accept(IOStatePtr state, IOInternalHandle handle)
{
    SocketIOStatePtr self = (SocketIOStatePtr) state;
    match_result(SocketHandlePtr, SocketIOState__get_handle(self, handle),
        success(listener)
        {
            if (listener->kind != SOCKET_HANDLE_LISTENER)
            {
                return Error(IOInternalHandle, socket_invalid_operation, IO_OPERATION_NOT_SUPPORTED);
            }

            critical_section(self->lock,
                match_optional(IOInternalHandle, SocketPendingConnections__pop_front(&listener->pending_connections),
                    some(connection_handle) { cs_return Success(IOInternalHandle, connection_handle); },
                    none() { cs_return Error(IOInternalHandle, socket_accept_queue_empty, IO_WAIT_FOR_DATA); }
                );
            );
        },
        error(code, message) { return Error(IOInternalHandle, message, code); }
    );

    return Error(IOInternalHandle, socket_invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOInternalHandle) SocketIO__close(IOStatePtr state, IOInternalHandle handle)
{
    SocketIOStatePtr self = (SocketIOStatePtr) state;
    SocketHandlePtr closed;
    match_result(SocketHandlePtr, SocketIOState__get_handle(self, handle),
        success(found) { closed = found; },
        error(code, message) { return Error(IOInternalHandle, message, code); }
    );

    critical_section(self->lock,
        SocketHandleMap__delete(&self->handles, handle);
        match_optional(IOInternalHandle, closed->peer,
            some(peer_handle)
            {
                match_optional(SocketHandlePtr, SocketHandleMap__get(&self->handles, peer_handle),
                    some(peer) { peer->peer = None(IOInternalHandle); },
                    none_do_nothing()
                );
            },
            none_do_nothing()
        );
    );

    SocketHandle__dispose(closed);
    return Success(IOInternalHandle, handle);
}

static Result(IOBufferPtr) SocketIO__read(
    IOStatePtr state,
    IOInternalHandle handle,
    size_t size,
    ConstExternalBufferPtr target
)
{
    SocketIOStatePtr self = (SocketIOStatePtr) state;
    match_result(SocketHandlePtr, SocketIOState__get_handle(self, handle),
        success(socket_handle)
        {
            if (IOBuffer__item_count(socket_handle->buffer) == 0)
            {
                return Error(IOBufferPtr, socket_request_pending, IO_WAIT_FOR_DATA);
            }
            return Success(IOBufferPtr, socket_handle->buffer);
        },
        error(code, message) { return Error(IOBufferPtr, message, code); }
    );

    return Error(IOBufferPtr, socket_invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(ByteCount) SocketIO__write(IOStatePtr state, IOInternalHandle handle, DataPtr data, size_t size)
{
    SocketIOStatePtr self = (SocketIOStatePtr) state;
    SocketHandlePtr socket_handle;
    SocketHandlePtr peer;

    match_result(SocketHandlePtr, SocketIOState__get_handle(self, handle),
        success(found) { socket_handle = found; },
        error(code, message) { return Error(ByteCount, message, code); }
    );

    match_optional(IOInternalHandle, socket_handle->peer,
        some(peer_handle)
        {
            match_result(SocketHandlePtr, SocketIOState__get_handle(self, peer_handle),
                success(found) { peer = found; },
                error(code, message) { return Error(ByteCount, message, code); }
            );
        },
        none() { return Error(ByteCount, socket_not_connected, IO_NOT_OPEN); }
    );

    Optional(IOContextPtr) parked_reader = None(IOContextPtr);
    critical_section(self->lock,
        match_result(Pos, IOBuffer__push_all(peer->buffer, (CharPtr) data, size),
            success_do_nothing(),
            error(code, message) { cs_return Error(ByteCount, message, code); }
        );
        parked_reader = peer->parked_reader;
        peer->parked_reader = None(IOContextPtr);
    );

    match_optional(IOContextPtr, parked_reader,
        some(context)
        {
            match_optional(ProcessID, context->kernel_context.pid,
                some(pid)
                {
                    match_optional(IOHandle, context->handle,
                        some(context_handle)
                        {
                            MessagePtr message = Message__heap_new_io_operation(
                                SOURCE_IO, SUBSYS_IO_OPERATION_AVAILABLE, pid, context_handle
                            );
                            match_result(AckCode, MessageBroker__deliver(self->message_broker, message),
                                success_do_nothing(),
                                error(code, message) { return Error(ByteCount, message, code); }
                            );
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    return Success(ByteCount, size);
}

static Result(ParkStatus) SocketIO__park(IOStatePtr state, IOContextPtr parked_context)
{
    SocketIOStatePtr self = (SocketIOStatePtr) state;
    match_optional(IOHandle, parked_context->handle,
        some(handle)
        {
            match_result(SocketHandlePtr, SocketIOState__get_handle(self, device_internal_handle(handle)),
                success(socket_handle)
                {
                    critical_section(self->lock,
                        socket_handle->parked_reader = Some(IOContextPtr, parked_context);
                    );
                    return Success(ParkStatus, IO_PARK_OK);
                },
                error(code, message) { return Error(ParkStatus, message, code); }
            );
        },
        none() { return Error(ParkStatus, socket_invalid_handle, IO_UNKNOWN_HANDLE); }
    );

    return Error(ParkStatus, socket_invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(ParkStatus) SocketIO__unpark(IOStatePtr state)
{
    return Success(ParkStatus, IO_UNPARK_OK);
}

SocketIOPtr SocketIO__heap_new(void)
{
    SocketIOPtr result = v_ptr(Kernel__alloc(sizeof(SocketIO)), SocketIO);
    result->base_io = IO__new(
        SocketIO__open,
        SocketIO__read,
        SocketIO__write,
        SocketIO__accept,
        SocketIO__close,
        SocketIO__park,
        SocketIO__unpark,
        IO__unsupported_server_buffer,
        IO__unsupported_server_write_buffer,
        IO__unsupported_data_available,
        IO__unsupported_data_consumed,
        IO__unsupported_eof
    );
    result->registry = SOCKET_REGISTRY_INIT;
    return result;
}

Result(SocketRegistryPtr) SocketIO__registered_registry(IOSubsystemPtr io_subsystem)
{
    match_optional(DeviceRegistrationPtr, IOSubsystem__find_registered_device(io_subsystem, &bstr(IO_SOCKET_SCHEME)),
        some(registration)
        {
            SocketIOPtr socket_io = (SocketIOPtr) registration->device;
            return Success(SocketRegistryPtr, &socket_io->registry);
        },
        none_do_nothing()
    );

    return Error(SocketRegistryPtr, socket_device_not_registered, IO_NO_DEVICE_FOUND);
}
