#include "stream_driver.h"
#include "path.h"

FixedArrayBodyDefine(IOStreamResources);

bool IOStreamResource__equals(void * first, void * second)
{
    IOStreamResourcePtr left = first;
    IOStreamResourcePtr right = second;
    return left->handle == right->handle;
}

IOStreamDriver IOStreamDriver__new(Flags access, IOServiceContext context, IOStreamTransfer transfer)
{
    return (IOStreamDriver){.resources = IOStreamResources__new(),
                            .next_handle = IO_SESSION_HANDLE_BASE,
                            .access = access,
                            .context = context,
                            .transfer = transfer};
}

static bool IOStreamResource__matches(IOStreamResourcePtr self, void * context)
{
    IORequestPtr request = context;
    return self->session == request->client_session && self->pid == request->client_pid &&
           (request->operation == IO_REQUEST_RELEASE_CLIENT || self->handle == request->handle);
}

static IOStreamResource IOStreamResource__new(IOHandle handle, IORequestPtr request)
{
    return (IOStreamResource){
        .handle = handle, .session = request->client_session, .pid = request->client_pid, .flags = request->flags};
}

static IOReply IOStreamDriver__request(IOStreamDriverPtr self, IORequestPtr request)
{
    if (request->client_pid == 0 || request->client_session == 0 || request->count > IORequest__capacity(request))
    {
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    if (request->operation == IO_REQUEST_RELEASE_CLIENT)
    {
        while (true)
        {
            match_find_result(
                IOStreamResources__find(&self->resources, IOStreamResource__matches, request),
                some(resource)
                {
                    match_result(
                        IOStreamResources__remove(&self->resources, resource),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return IOReply__new(IO_UNKNOWN_HANDLE, 0);
                        }
                    );
                },
                none()
                {
                    return IOReply__new(IO_OK, 0);
                }
            );
        }
    }
    if (request->operation == IO_REQUEST_OPEN)
    {
        BString path = IORequest__path(request);
        if (!IOPath__is_stream(&path))
        {
            return IOReply__new(IO_INVALID_PATH, 0);
        }
        if (request->flags != self->access)
        {
            return IOReply__new(IO_PERMISSION_DENIED, 0);
        }
        if (self->next_handle == INT64_MAX)
        {
            return IOReply__new(IO_REGISTER_FAILED, 0);
        }
        IOStreamResource resource = IOStreamResource__new(self->next_handle++, request);
        match_result(
            IOStreamResources__push(&self->resources, resource),
            success(code)
            {
                return IOReply__new(IO_OK, resource.handle);
            },
            error(code, message)
            {
                return IOReply__new(IO_REGISTER_FAILED, 0);
            }
        );
    }
    match_find_result(
        IOStreamResources__find(&self->resources, IOStreamResource__matches, request),
        some(resource)
        {
            if (request->operation == IO_REQUEST_CLOSE)
            {
                match_result(
                    IOStreamResources__remove(&self->resources, resource),
                    success(code)
                    {
                        return IOReply__new(IO_OK, 0);
                    },
                    error(code, message)
                    {
                        return IOReply__new(IO_UNKNOWN_HANDLE, 0);
                    }
                );
            }
            if ((request->operation == IO_REQUEST_READ && self->access == IO_OPEN_READ) ||
                (request->operation == IO_REQUEST_WRITE && self->access == IO_OPEN_WRITE) ||
                (request->operation == IO_REQUEST_SET_FOREGROUND_PROCESS && self->access == IO_OPEN_READ))
            {
                return self->transfer(self->context, request);
            }
            return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
        },
        none()
        {
            return IOReply__new(IO_UNKNOWN_HANDLE, 0);
        }
    );
}

Optional(IOReply)
    IOStreamDriver__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    IOStreamDriverPtr self = context;
    if (pid == 0)
    {
        return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
    }
    IOReply reply = IOStreamDriver__request(self, request);
    if (request->operation == IO_REQUEST_READ && reply.status == IO_WAIT_FOR_DATA)
    {
        return None(IOReply);
    }
    return Some(IOReply, reply);
}

bool IOStreamDriver__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    IOStreamDriverPtr self = context;
    IORequest owner = IORequest__new(IO_REQUEST_RELEASE_CLIENT, 0);
    owner.client_session = session;
    owner.client_pid = pid;
    IOStreamDriver__request(self, &owner);
    return true;
}
