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
                            .inheritance = IOInheritanceRegistry__new(),
                            .next_handle = IO_SESSION_HANDLE_BASE,
                            .access = access,
                            .context = context,
                            .transfer = transfer};
}

IOStreamDriver IOStreamDriver__new_terminal(Flags access, IOServiceContext context, IOStreamTransfer transfer)
{
    IOStreamDriver driver = IOStreamDriver__new(access, context, transfer);
    driver.terminal = true;
    driver.terminal_attributes = IOTerminalAttributes__default();
    return driver;
}

static bool IOStreamResource__matches(IOStreamResourcePtr self, void * context)
{
    IORequestPtr request = context;
    return self->handle == request->handle &&
           IOResourceOwners__contains(&self->owners, request->client_session, request->client_pid);
}

static IOStreamResource IOStreamResource__new(IOHandle handle, IORequestPtr request)
{
    IOStreamResource resource = {.handle = handle, .owners = IOResourceOwners__new(), .flags = request->flags};
    IOResourceOwners__add(&resource.owners, request->client_session, request->client_pid);
    return resource;
}

static bool IOStreamResource__has_handle(IOStreamResourcePtr self, void * context)
{
    return self->handle == *(IOHandlePtr)context;
}

static bool IOStreamResource__owned(IOStreamResourcePtr self, void * context)
{
    IOResourceOwnerPtr owner = context;
    return IOResourceOwners__contains(&self->owners, owner->session, owner->pid);
}

static bool IOStreamDriver__release_owner(IOStreamDriverPtr self, QueueConnectionID session, uint64_t pid)
{
    IOResourceOwner owner = IOResourceOwner__new(session, pid);
    while (true)
    {
        match_find_result_ptr(
            IOStreamResources__find(&self->resources, IOStreamResource__owned, &owner),
            some(resource)
            {
                if (IOResourceOwners__remove_owner(&resource->owners, session, pid) != IO_OK)
                {
                    return false;
                }
                if (IOResourceOwners__len(&resource->owners) == 0 &&
                    !IOInheritanceRegistry__retains_handle(&self->inheritance, resource->handle))
                {
                    IOStreamResource removed = *resource;
                    match_result(
                        IOStreamResources__remove(&self->resources, removed),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return false;
                        }
                    );
                }
            },
            none()
            {
                IOInheritanceRegistry__release(&self->inheritance, session, pid);
                return true;
            }
        );
    }
}

static IOReply IOStreamDriver__request(IOStreamDriverPtr self, IORequestPtr request)
{
    if (request->client_pid == 0 || request->client_session == 0 || request->count > IORequest__capacity(request))
    {
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    if (request->operation == IO_REQUEST_RELEASE_CLIENT)
    {
        return IOReply__new(IOStreamDriver__release_owner(self, request->client_session, request->client_pid)
                                ? IO_OK
                                : IO_UNKNOWN_HANDLE,
                            0);
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
    if (request->operation == IO_REQUEST_INHERIT_COMMIT)
    {
        return IOReply__new(IOInheritanceRegistry__commit(&self->inheritance, request), 0);
    }
    if (request->operation == IO_REQUEST_INHERIT_ABORT)
    {
        return IOReply__new(IOInheritanceRegistry__abort(&self->inheritance, request), 0);
    }
    if (request->operation == IO_REQUEST_INHERIT_CLAIM)
    {
        match_optional(
            IOInheritanceRegistry__claim(&self->inheritance, request),
            some(grant)
            {
                match_find_result_ptr(
                    IOStreamResources__find(&self->resources, IOStreamResource__has_handle, &grant->handle),
                    some(resource)
                    {
                        IOStatus status =
                            IOResourceOwners__add(&resource->owners, request->client_session, request->client_pid);
                        if (status != IO_OK)
                        {
                            return IOReply__new(status, 0);
                        }
                        IOReply reply = IOReply__new(IO_OK, resource->handle);
                        reply.inheritance_flags = grant->flags;
                        IOInheritanceRegistry__complete(&self->inheritance, grant);
                        return reply;
                    },
                    none()
                    {
                        return IOReply__new(IO_UNKNOWN_HANDLE, 0);
                    }
                );
            },
            none()
            {
                return IOReply__new(IO_PERMISSION_DENIED, 0);
            }
        );
    }
    match_find_result_ptr(
        IOStreamResources__find(&self->resources, IOStreamResource__matches, request),
        some(resource)
        {
            if (request->operation == IO_REQUEST_INHERIT_PREPARE)
            {
                return IOInheritanceRegistry__prepare(&self->inheritance, resource->handle, resource->flags,
                                                      request->client_session, request->client_pid);
            }
            if (request->operation == IO_REQUEST_CLOSE)
            {
                if (IOResourceOwners__remove_owner(&resource->owners, request->client_session, request->client_pid) !=
                    IO_OK)
                {
                    return IOReply__new(IO_UNKNOWN_HANDLE, 0);
                }
                if (IOResourceOwners__len(&resource->owners) == 0 &&
                    !IOInheritanceRegistry__retains_handle(&self->inheritance, resource->handle))
                {
                    IOStreamResource removed = *resource;
                    match_result(
                        IOStreamResources__remove(&self->resources, removed),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return IOReply__new(IO_UNKNOWN_HANDLE, 0);
                        }
                    );
                }
                return IOReply__new(IO_OK, 0);
            }
            if (self->terminal && request->operation == IO_REQUEST_GET_TERMINAL_ATTRIBUTES)
            {
                IOReply reply = IOReply__new(IO_OK, 0);
                reply.terminal_attributes = self->terminal_attributes;
                return reply;
            }
            if (self->terminal && request->operation == IO_REQUEST_SET_TERMINAL_ATTRIBUTES)
            {
                self->terminal_attributes = request->terminal_attributes;
                return IOReply__new(IO_OK, 0);
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
    return IOStreamDriver__release_owner(self, session, pid);
}
