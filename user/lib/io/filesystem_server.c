#include "filesystem_driver.h"
#include "sys/mem.h"
#include "sys/service.h"

static Optional(IOReply)
    FilesystemDriver__serve(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    FilesystemDriverPtr self = context;
    uint64_t namespace_pid = unwrap_optional(
        ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr(IO_FILESYSTEM_SERVICE)), 0);
    if (pid == 0 || pid != namespace_pid || (self->namespace_session != 0 && self->namespace_session != session))
    {
        return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
    }
    self->namespace_session = session;
    return Some(IOReply, FilesystemDriver__execute(self, request));
}

static bool FilesystemDriver__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    FilesystemDriverPtr self = context;
    if (self->namespace_session == session)
    {
        for_each_fa_ptr(
            FilesystemFiles,
            (&self->files),
            index,
            file,
            {
                free(file.data);
            }
        );
        self->files = FilesystemFiles__new();
        self->namespace_session = 0;
    }
    return true;
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
