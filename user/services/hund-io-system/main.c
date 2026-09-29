#include "io/path.h"
#include "io/server.h"
#include "io/stream_registry.h"
#include "sys/boot_info.h"
#include "sys/service.h"

typedef struct IOSystem
{
    StreamRegistry streams;
} IOSystem;

typedef IOSystem * IOSystemPtr;

static IOSystem IOSystem__new(void)
{
    return (IOSystem){.streams = StreamRegistry__new()};
}

static Optional(IOReply)
    IOSystem__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    IOSystemPtr self = context;
    if (request->operation != IO_REQUEST_OPEN && request->operation != IO_REQUEST_LIST_DIR &&
        request->operation != IO_REQUEST_FILE_INFO && request->operation != IO_REQUEST_MKDIR &&
        request->operation != IO_REQUEST_REMOVE && request->operation != IO_REQUEST_LINK)
    {
        return Some(IOReply, IOReply__new(IO_UNKNOWN_HANDLE, 0));
    }

    BString path = IORequest__path(request);
    if (request->operation == IO_REQUEST_OPEN && IOPath__is_stream(&path))
    {
        return Some(IOReply, StreamRegistry__route(&self->streams, request));
    }

    BString target_path = IORequest__target_path(request);
    BString service =
        BString__starts_with(&path, &bstr(IO_SOCKET_SCHEME)) ? bstr(IO_SOCKET_SERVICE) : bstr(IO_FILESYSTEM_SERVICE);
    bool final = BString__equals(&service, &bstr(IO_SOCKET_SERVICE));
    IOReply reply = IOReply__redirect(&service, &path, &target_path, final);
    if (request->operation == IO_REQUEST_LIST_DIR)
    {
        reply.value = request->offset;
    }
    return Some(IOReply, reply);
}

static bool IOSystem__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    return true;
}

static Result(IOStatus) IOSystem__register_streams(IOSystemPtr self, bool video)
{
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

#ifndef HUND_IO_SYSTEM_NO_MAIN
int main(void)
{
    static IOSystem router;
    static IOService service;
    router = IOSystem__new();
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
        IOSystem__register_streams(&router, video),
        success_do_nothing(),
        error(code, message)
        {
            StreamRegistry__dispose(&router.streams);
            return 1;
        }
    );
    if (IOService__listen_into(&service, &bstr(IO_SYSTEM_SERVICE), &router, IOSystem__execute, IOSystem__disconnect) !=
        IO_OK)
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
#endif
