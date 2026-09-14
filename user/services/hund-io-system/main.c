#include "io/mediator.h"
#include "sys/service.h"

typedef struct IOSystem
{
    IOMediator sockets;
    IOMediator filesystem;
    bool filesystem_connected;
} IOSystem;
typedef IOSystem * IOSystemPtr;

static Optional(IOReply)
    IOSystem__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    IOSystemPtr self = context;
    BString path = IORequest__path(request);
    bool socket = request->operation == IO_REQUEST_OPEN
                      ? BString__starts_with(&path, &bstr(IO_SOCKET_SCHEME))
                      : request->handle >= IO_SESSION_HANDLE_BASE && request->handle < IO_FILESYSTEM_HANDLE_BASE;
    if (socket)
    {
        return IOMediator__execute(&self->sockets, session, pid, request);
    }
    if (!self->filesystem_connected)
    {
        match_result(
            Channel__open(ServiceDirectory__shared(), &bstr(IO_FILESYSTEM_SERVICE)),
            success(channel)
            {
                self->filesystem = IOMediator__new_filesystem(channel, IO_FILESYSTEM_HANDLE_BASE);
                self->filesystem_connected = true;
            },
            error(code, message) { return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0)); }
        );
    }
    return IOMediator__execute(&self->filesystem, session, pid, request);
}

static bool IOSystem__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    IOSystemPtr self = context;
    bool sockets = IOMediator__disconnect(&self->sockets, session, pid);
    bool filesystem = !self->filesystem_connected || IOMediator__disconnect(&self->filesystem, session, pid);
    return sockets && filesystem;
}

int main(void)
{
    static IOSystem mediator;
    static IOService service;
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    match_result(
        IOService__listen(&bstr(IO_SYSTEM_SERVICE), &mediator, IOSystem__execute, IOSystem__disconnect),
        success(created) { service = created; },
        error(code, message) { return 1; }
    );
    match_result(
        Channel__open(ServiceDirectory__shared(), &bstr(IO_SOCKET_SERVICE)),
        success(backend) { mediator.sockets = IOMediator__new(backend); },
        error(code, message) { return 1; }
    );
    if (!service_ok(service_publish_ready(&bstr(IO_SYSTEM_SERVICE))))
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
