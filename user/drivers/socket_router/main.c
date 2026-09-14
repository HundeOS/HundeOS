#include "sockets.h"
#include "io/server.h"
#include "sys/service.h"

typedef struct SocketServer
{
    SocketService sockets;
    QueueConnectionID mediator;
} SocketServer;
typedef SocketServer * SocketServerPtr;

static SocketServer SocketServer__new(void)
{
    return (SocketServer){.sockets = SocketService__new()};
}

static Optional(IOReply)
    SocketServer__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    SocketServerPtr self = context;
    uint64_t mediator_pid =
        unwrap_optional(ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr(IO_SYSTEM_SERVICE)), 0);
    if (pid == 0 || pid != mediator_pid || (self->mediator != 0 && self->mediator != session))
    {
        return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
    }
    self->mediator = session;
    return Some(IOReply, SocketService__execute(&self->sockets, request));
}

static bool SocketServer__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    SocketServerPtr self = context;
    if (self->mediator == session)
    {
        self->sockets.resources = SocketResources__new();
        self->mediator = 0;
    }
    return true;
}

int main(void)
{
    static SocketServer server;
    static IOService service;
    server = SocketServer__new();
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    match_result(
        IOService__listen(&bstr(IO_SOCKET_SERVICE), &server, SocketServer__execute, SocketServer__disconnect),
        success(created) { service = created; },
        error(code, message) { return 1; }
    );
    if (!service_ok(service_publish_ready(&bstr(IO_SOCKET_SERVICE))))
    {
        return 1;
    }
    Channel__prepare_wait();
    while (true)
    {
        if (!IOService__poll(&service))
        {
            Channel__wait_event();
        }
    }
}
