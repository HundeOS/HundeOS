#define DRIVER_LOG_TAG "SOCKET_ROUTER"

#include <stdbool.h>
#include <stdint.h>

#include "async_io/async_io.h"
#include "sys/debug.h"
#include "sys/io.h"
#include "sys/service.h"
#include "sys/sync.h"
#include "syscalls/socket.h"
#include "types/fixed_array.h"
#include "types/result.h"

#define SOCKET_ROUTER_READY_NAME "socket_router"
#define SOCKET_ROUTER_MAX_ENDPOINTS 64
#define SOCKET_ROUTER_MAX_PENDING_CONNECTIONS 64

typedef struct SocketRouterConnection
{
    IOHandle client;
}
SocketRouterConnection;

typedef SocketRouterConnection * SocketRouterConnectionPtr;

OptionalDef(SocketRouterConnection);

static SocketRouterConnection SocketRouterConnection__new(IOHandle client)
{
    return (SocketRouterConnection) {
        .client = client,
    };
}

static bool SocketRouterConnection__equals(SocketRouterConnectionPtr self, SocketRouterConnectionPtr other)
{
    return self->client == other->client;
}

FixedArrayDefine(
    SocketRouterConnection,
    SOCKET_ROUTER_MAX_PENDING_CONNECTIONS,
    SocketRouterConnections,
    (EqualFunction) SocketRouterConnection__equals
);
FixedArrayBodyDefine(SocketRouterConnections);

typedef struct SocketRouterEndpoint
{
    BString uri;
    IOHandle listener;
    uint64_t backlog;
    SocketRouterConnections pending;
}
SocketRouterEndpoint;

typedef SocketRouterEndpoint * SocketRouterEndpointPtr;

OptionalDef(SocketRouterEndpoint);

static SocketRouterEndpoint SocketRouterEndpoint__new(ConstBStringPtr uri, IOHandle listener, uint64_t backlog)
{
    return (SocketRouterEndpoint) {
        .uri = *uri,
        .listener = listener,
        .backlog = backlog,
        .pending = SocketRouterConnections__new(),
    };
}

static bool SocketRouterEndpoint__equals(SocketRouterEndpointPtr self, SocketRouterEndpointPtr other)
{
    return BString__equals(&self->uri, &other->uri);
}

FixedArrayDefine(
    SocketRouterEndpoint,
    SOCKET_ROUTER_MAX_ENDPOINTS,
    SocketRouterEndpoints,
    (EqualFunction) SocketRouterEndpoint__equals
);
FixedArrayBodyDefine(SocketRouterEndpoints);

typedef struct SocketRouter
{
    SocketRouterEndpoints endpoints;
}
SocketRouter;

typedef SocketRouter * SocketRouterPtr;

ResultDef(IOSocketStatus);
ResultDef(IOHandle);

static BString socket_router_invalid_uri = bstr("invalid socket uri");
static BString socket_router_endpoint_not_found = bstr("socket endpoint not found");
static BString socket_router_endpoint_exists = bstr("socket endpoint already exists");
static BString socket_router_backlog_full = bstr("socket listener backlog full");
static BString socket_router_no_pending_connection = bstr("socket listener has no pending connection");
static BString socket_router_endpoints_full = bstr("socket endpoint table full");
static BString socket_router_service_registration_failed = bstr("socket router service registration failed");
static BString socket_router_ready_publish_failed = bstr("socket router ready publish failed");

static SocketRouter SocketRouter__new(void)
{
    return (SocketRouter) {
        .endpoints = SocketRouterEndpoints__new(),
    };
}

static bool SocketRouter__uri_valid(ConstBStringPtr uri)
{
    return BString__starts_with(uri, &bstr(IO_SOCKET_SCHEME)) && BString__length(uri) > BString__length(&bstr(IO_SOCKET_SCHEME));
}

static bool SocketRouterEndpoint__uri_matches(SocketRouterEndpointPtr endpoint, void * raw_uri)
{
    ConstBStringPtr uri = (ConstBStringPtr) raw_uri;
    return BString__equals(&endpoint->uri, uri);
}

static bool SocketRouterEndpoint__listener_matches(SocketRouterEndpointPtr endpoint, void * raw_listener)
{
    IOHandle * listener = (IOHandle *) raw_listener;
    return endpoint->listener == *listener;
}

static Result(IOSocketStatus) SocketRouter__listen(
    SocketRouterPtr self,
    ConstBStringPtr uri,
    IOHandle listener,
    uint64_t backlog
)
{
    if (!SocketRouter__uri_valid(uri))
    {
        return Error(IOSocketStatus, socket_router_invalid_uri, IO_SOCKET_INVALID_URI);
    }

    match_find_result_ptr(SocketRouterEndpoints, SocketRouterEndpoints__find(
        &self->endpoints, SocketRouterEndpoint__uri_matches, (void *) uri
    ),
        some(endpoint) { return Error(IOSocketStatus, socket_router_endpoint_exists, IO_SOCKET_ENDPOINT_EXISTS); },
        none_do_nothing()
    );

    SocketRouterEndpoint endpoint = SocketRouterEndpoint__new(uri, listener, backlog);
    match_result(CollectionCode, SocketRouterEndpoints__push(&self->endpoints, endpoint),
        success(_) { return Success(IOSocketStatus, IO_SOCKET_OK); },
        error(code, message) { return Error(IOSocketStatus, socket_router_endpoints_full, IO_SOCKET_ENDPOINTS_FULL); }
    );

    return Error(IOSocketStatus, socket_router_endpoints_full, IO_SOCKET_ENDPOINTS_FULL);
}

static Result(IOHandle) SocketRouter__connect(
    SocketRouterPtr self,
    ConstBStringPtr uri,
    IOHandle client
)
{
    match_find_result_ptr(SocketRouterEndpoints, SocketRouterEndpoints__find(
        &self->endpoints, SocketRouterEndpoint__uri_matches, (void *) uri
    ),
        some(endpoint)
        {
            if (SocketRouterConnections__len(&endpoint->pending) >= endpoint->backlog)
            {
                return Error(IOHandle, socket_router_backlog_full, IO_SOCKET_BACKLOG_FULL);
            }

            match_result(CollectionCode, SocketRouterConnections__push(
                &endpoint->pending, SocketRouterConnection__new(client)
            ),
                success(_) { return Success(IOHandle, endpoint->listener); },
                error(code, message) { return Error(IOHandle, socket_router_backlog_full, IO_SOCKET_BACKLOG_FULL); }
            );
        },
        none() { return Error(IOHandle, socket_router_endpoint_not_found, IO_SOCKET_ENDPOINT_NOT_FOUND); }
    );

    return Error(IOHandle, socket_router_endpoint_not_found, IO_SOCKET_ENDPOINT_NOT_FOUND);
}

static Result(IOHandle) SocketRouter__accept(SocketRouterPtr self, IOHandle listener)
{
    match_find_result_ptr(SocketRouterEndpoints, SocketRouterEndpoints__find(
        &self->endpoints, SocketRouterEndpoint__listener_matches, &listener
    ),
        some(endpoint)
        {
            match_optional(SocketRouterConnection, SocketRouterConnections__pop(&endpoint->pending),
                some(connection) { return Success(IOHandle, connection.client); },
                none() { return Error(IOHandle, socket_router_no_pending_connection, IO_SOCKET_NO_PENDING_CONNECTION); }
            );
        },
        none() { return Error(IOHandle, socket_router_endpoint_not_found, IO_SOCKET_NOT_LISTENER); }
    );

    return Error(IOHandle, socket_router_endpoint_not_found, IO_SOCKET_NOT_LISTENER);
}

static Result(IOHandle) SocketRouter__handle_request(
    SocketRouterPtr self,
    IOSocketRouterRequestPtr request
)
{
    switch (request->operation)
    {
        case IO_SOCKET_ROUTER_LISTEN:
            match_result(IOSocketStatus, SocketRouter__listen(self, &request->uri, request->handle, request->backlog),
                success(status) { return Success(IOHandle, request->handle); },
                error(code, message) { return Error(IOHandle, message, code); }
            );

        case IO_SOCKET_ROUTER_CONNECT:
            match_result(IOHandle, SocketRouter__connect(self, &request->uri, request->handle),
                success(listener) { return Success(IOHandle, listener); },
                error(code, message) { return Error(IOHandle, message, code); }
            );

        case IO_SOCKET_ROUTER_ACCEPT:
            return SocketRouter__accept(self, request->handle);
    }

    return Error(IOHandle, socket_router_invalid_uri, IO_SOCKET_INVALID_URI);
}

static Result(AsyncIOCode) SocketRouter__register_service(Anything service_context)
{
    if (!service_ok(service_become_service()))
    {
        return Error(AsyncIOCode, socket_router_service_registration_failed, ASYNC_IO_REGISTER_FAILED);
    }

    if (!service_ok(service_register_socket_router()))
    {
        return Error(AsyncIOCode, socket_router_service_registration_failed, ASYNC_IO_REGISTER_FAILED);
    }

    if (!service_ok(service_publish_ready(&bstr(SOCKET_ROUTER_READY_NAME))))
    {
        return Error(AsyncIOCode, socket_router_ready_publish_failed, ASYNC_IO_REGISTER_FAILED);
    }

    return Success(AsyncIOCode, ASYNC_IO_OK);
}

static Result(FiberCode) SocketRouter__request_service(FiberPtr fiber, Anything service_context, ServiceEvent request)
{
    SocketRouterPtr router = (SocketRouterPtr) service_context;
    uint64_t request_id = ServiceEvent__request_id(&request);
    IOSocketRouterRequest router_request;
    ServiceCode get_status = service_socket_get_request(request_id, &router_request);
    if (!service_ok(get_status))
    {
        return Error(FiberCode, socket_router_endpoint_not_found, get_status);
    }

    match_result(IOHandle, SocketRouter__handle_request(router, &router_request),
        success(handle)
        {
            ServiceCode complete_status = service_socket_complete(request_id, IO_SOCKET_OK, handle);
            if (!service_ok(complete_status))
            {
                return Error(FiberCode, socket_router_endpoint_not_found, complete_status);
            }
            return Success(FiberCode, FIBER_OK);
        },
        error(code, message)
        {
            ServiceCode complete_status = service_socket_complete(request_id, (IOSocketStatus) code, -1);
            if (!service_ok(complete_status))
            {
                return Error(FiberCode, message, complete_status);
            }
            return Success(FiberCode, FIBER_OK);
        }
    );

    return Error(FiberCode, socket_router_invalid_uri, FIBER_INVALID_STATE);
}

int main(void)
{
    SocketRouter router = SocketRouter__new();
    AsyncIO async_io;
    match_result(AsyncIOCode, AsyncIO__start(
        &async_io,
        SocketRouter__register_service,
        (Anything) &router,
        SocketRouter__request_service
    ),
        success(_) { return 0; },
        error(code, message)
        {
            DRIVER_DEBUG("socket_router: async io failed code=%i\n", code);
            return 1;
        }
    );

    return 1;
}
