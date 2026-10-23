#include "bootstrap.h"
#include "sys/service.h"

Optional(IOReply)
    BootstrapArchive__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    BootstrapArchivePtr self = context;
    if (request->operation == IO_REQUEST_BOOTSTRAP_RETIRE)
    {
        if (!service_ok(service_authorize_mount_client(pid)) || request->count != 0 || request->flags != 0 ||
            request->handle != 0)
        {
            return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
        }
        self->draining = true;
        if (!Embedfs__is_idle(&self->filesystem) || !IOService__is_idle_except(&self->service, session))
        {
            return None(IOReply);
        }
        return Some(IOReply, IOReply__new(IO_OK, 0));
    }
    if (self->draining && (request->operation == IO_REQUEST_OPEN || request->operation == IO_REQUEST_LIST_DIR ||
                           request->operation == IO_REQUEST_FILE_INFO))
    {
        BString relative = IORequest__path(request);
        if (BString__starts_with(&relative, &bstr("/")))
        {
            relative = BString__slice(&relative, 1, BString__length(&relative));
        }
        if (BString__length(&relative) + 6 >= IO_PROTOCOL_DATA_SIZE)
        {
            return Some(IOReply, IOReply__new(IO_INVALID_PATH, 0));
        }
        BString path = BString__append(&bstr("/sbin/"), &relative);
        IOReply reply = IOReply__redirect(&bstr(IO_SYSTEM_SERVICE), &path, &empty_bstr(), false);
        reply.value = request->offset;
        return Some(IOReply, reply);
    }
    return Embedfs__serve(&self->filesystem, session, pid, request);
}

bool BootstrapArchive__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    BootstrapArchivePtr self = context;
    return Embedfs__disconnect(&self->filesystem, session, pid);
}
