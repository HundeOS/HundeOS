#include "sockets.h"
#include "io/server.h"
#include "sys/service.h"

typedef struct SocketServer
{
    SocketService sockets;
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
    if (pid == 0)
    {
        return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
    }
    IOReply reply = SocketService__execute(&self->sockets, request);
    return reply.status == IO_WAIT_FOR_DATA ? None(IOReply) : Some(IOReply, reply);
}

static bool SocketServer__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    SocketServerPtr self = context;
    SocketService__release(&self->sockets, session);
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
    if (IOService__listen_into(&service, &bstr(IO_SOCKET_SERVICE), &server, SocketServer__execute,
                               SocketServer__disconnect) != IO_OK)
    {
        return 1;
    }
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
