#include "io/mediator.h"
#include "io/stream_registry.h"
#include "io/path.h"
#include "sys/service.h"
#include "sys/boot_info.h"

typedef struct IOSystem
{
    IOMediator sockets;
    bool sockets_connected;
    IOMediator filesystem;
    bool filesystem_connected;
    StreamRegistry streams;
} IOSystem;
typedef IOSystem * IOSystemPtr;

static Optional(IOReply)
    IOSystem__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    IOSystemPtr self = context;
    BString path = IORequest__path(request);
    bool terminal = request->operation == IO_REQUEST_OPEN ? BString__starts_with(&path, &bstr(":"))
                                                          : request->handle >= IO_STREAM_HANDLE_BASE;
    if (terminal)
    {
        return StreamRegistry__execute(&self->streams, session, pid, request);
    }
    bool socket = request->operation == IO_REQUEST_OPEN
                      ? BString__starts_with(&path, &bstr(IO_SOCKET_SCHEME))
                      : request->handle >= IO_SESSION_HANDLE_BASE && request->handle < IO_FILESYSTEM_HANDLE_BASE;
    if (socket)
    {
        if (!self->sockets_connected)
        {
            match_result(
                Channel__open(ServiceDirectory__shared(), &bstr(IO_SOCKET_SERVICE)),
                success(channel)
                {
                    self->sockets = IOMediator__new(channel);
                    self->sockets_connected = true;
                },
                error(code, message)
                {
                    return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0));
                }
            );
        }
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
            error(code, message)
            {
                return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0));
            }
        );
    }
    return IOMediator__execute(&self->filesystem, session, pid, request);
}

static bool IOSystem__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    IOSystemPtr self = context;
    bool sockets = !self->sockets_connected || IOMediator__disconnect(&self->sockets, session, pid);
    bool filesystem = !self->filesystem_connected || IOMediator__disconnect(&self->filesystem, session, pid);
    bool streams = StreamRegistry__disconnect(&self->streams, session, pid);
    return sockets && filesystem && streams;
}

static Result(IOStatus) IOSystem__register_streams(IOSystemPtr self, bool video)
{
    self->streams = StreamRegistry__new();
    match_result(
        StreamRegistry__register(&self->streams, &bstr(":keyboard:"), IO_OPEN_READ, &bstr(IO_KEYBOARD_SERVICE)),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    match_result(
        StreamRegistry__register(&self->streams, &bstr(":tty:"), IO_OPEN_READ, &bstr(IO_UART_RX_SERVICE)),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    match_result(
        StreamRegistry__register(&self->streams, &bstr(":tty:"), IO_OPEN_WRITE, &bstr(IO_UART_TX_SERVICE)),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    match_result(
        StreamRegistry__register(&self->streams, &bstr(":console:"), IO_OPEN_READ, &bstr(IO_UART_RX_SERVICE)),
        success_do_nothing(),
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    BString output = video ? bstr(IO_VIDEO_SERVICE) : bstr(IO_UART_TX_SERVICE);
    return StreamRegistry__register(&self->streams, &bstr(":console:"), IO_OPEN_WRITE, &output);
}

int main(void)
{
    static IOSystem mediator;
    static IOService service;
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    bool video = false;
    match_result(
        BootHardwareInfo__load(),
        success(info)
        {
            video = BootHardwareInfo__has_framebuffer(&info);
        },
        error(code, message)
        {
            return 1;
        }
    );
    match_result(
        IOSystem__register_streams(&mediator, video),
        success_do_nothing(),
        error(code, message)
        {
            StreamRegistry__dispose(&mediator.streams);
            return 1;
        }
    );
    if (IOService__listen_into(&service, &bstr(IO_SYSTEM_SERVICE), &mediator, IOSystem__execute,
                               IOSystem__disconnect) != IO_OK)
    {
        return 1;
    }
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
