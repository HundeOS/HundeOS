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
        if (!IOService__poll(&service))
        {
            Channel__wait_event();
        }
    }
}
