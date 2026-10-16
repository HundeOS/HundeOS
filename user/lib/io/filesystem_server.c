#include "filesystem_driver.h"
#include "sys/mem.h"
#include "sys/service.h"

static Optional(IOReply)
    FilesystemDriver__serve(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    FilesystemDriverPtr self = context;
    if (pid == 0)
    {
        return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
    }
    IOReply reply = FilesystemDriver__execute(self, request);
    if (request->operation == IO_REQUEST_WAIT_FILE_CHANGE && reply.status == IO_WAIT_FOR_DATA)
    {
        return None(IOReply);
    }
    return Some(IOReply, reply);
}

static bool FilesystemDriver__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    FilesystemDriverPtr self = context;
    return FilesystemDriver__release(self, session, pid);
}

static void FilesystemDriver__handle_service_events(FilesystemDriverPtr self)
{
    ServiceEvent event;
    for (ServiceCode status = service_poll(&event); status == SERVICE_OK; status = service_poll(&event))
    {
        if (event.type != SERVICE_EVENT_MEMORY_PRESSURE)
        {
            continue;
        }
        match_optional(
            self->memory_pressure_handler,
            some(handler)
            {
                handler(self->volume, (MemPressureLevel)event.source);
            },
            none_do_nothing()
        );
    }
}

int FilesystemDriver__run(FilesystemDriverPtr self, ConstBStringPtr mountpoint)
{
    static IOService service;
    if (IOService__listen_into(&service, mountpoint, self, FilesystemDriver__serve, FilesystemDriver__disconnect) !=
        IO_OK)
    {
        return 1;
    }

    if (!service_ok(service_publish_ready(mountpoint)))
    {
        return 1;
    }
    Channel__prepare_wait();
    while (true)
    {
        FilesystemDriver__handle_service_events(self);
        if (!IOService__poll(&service))
        {
            Channel__wait_event();
        }
    }
}
