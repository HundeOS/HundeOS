#include "inheritance.h"

FixedArrayBodyDefine(IOResourceOwners);
FixedArrayBodyDefine(IOInheritanceGrants);

IOResourceOwner IOResourceOwner__new(QueueConnectionID session, uint64_t pid)
{
    return (IOResourceOwner){.session = session, .pid = pid};
}

bool IOResourceOwner__equals(void * first, void * second)
{
    IOResourceOwnerPtr left = first;
    IOResourceOwnerPtr right = second;
    return left->session == right->session && left->pid == right->pid;
}

bool IOResourceOwners__contains(IOResourceOwners * self, QueueConnectionID session, uint64_t pid)
{
    IOResourceOwner owner = IOResourceOwner__new(session, pid);
    return is_something(IOResourceOwners__find_item(self, owner));
}

bool IOResourceOwners__contains_pid(IOResourceOwners * self, uint64_t pid)
{
    for_each_fa_ptr(
        IOResourceOwners,
        self,
        index,
        owner,
        {
            if (owner.pid == pid)
            {
                return true;
            }
        }
    );

    return false;
}

IOStatus IOResourceOwners__add(IOResourceOwners * self, QueueConnectionID session, uint64_t pid)
{
    IOResourceOwner owner = IOResourceOwner__new(session, pid);
    if (is_something(IOResourceOwners__find_item(self, owner)))
    {
        return IO_OK;
    }
    match_result(
        IOResourceOwners__push(self, owner),
        success(code)
        {
            return IO_OK;
        },
        error(code, message)
        {
            return IO_REGISTER_FAILED;
        }
    );
}

IOStatus IOResourceOwners__remove_owner(IOResourceOwners * self, QueueConnectionID session, uint64_t pid)
{
    IOResourceOwner owner = IOResourceOwner__new(session, pid);
    match_result(
        IOResourceOwners__remove(self, owner),
        success(code)
        {
            return IO_OK;
        },
        error(code, message)
        {
            return IO_UNKNOWN_HANDLE;
        }
    );
}

bool IOInheritanceGrant__equals(void * first, void * second)
{
    return ((IOInheritanceGrantPtr)first)->token == ((IOInheritanceGrantPtr)second)->token;
}

static IOInheritanceGrant IOInheritanceGrant__new(uint64_t token, IOHandle handle, Flags flags,
                                                  QueueConnectionID session, uint64_t pid)
{
    return (IOInheritanceGrant){
        .token = token, .handle = handle, .flags = flags, .source = IOResourceOwner__new(session, pid)};
}

IOInheritanceRegistry IOInheritanceRegistry__new(void)
{
    return (IOInheritanceRegistry){.grants = IOInheritanceGrants__new(), .next_token = 1};
}

static bool IOInheritanceGrant__matches_token(IOInheritanceGrantPtr self, void * context)
{
    return self->token == *(uint64_t *)context;
}

static Optional(IOInheritanceGrantPtr) IOInheritanceRegistry__find(IOInheritanceRegistryPtr self, uint64_t token)
{
    match_find_result_ptr(
        IOInheritanceGrants__find(&self->grants, IOInheritanceGrant__matches_token, &token),
        some(grant)
        {
            return Some(IOInheritanceGrantPtr, grant);
        },
        none()
        {
            return None(IOInheritanceGrantPtr);
        }
    );
}

IOReply IOInheritanceRegistry__prepare(IOInheritanceRegistryPtr self, IOHandle handle, Flags flags,
                                       QueueConnectionID session, uint64_t pid)
{
    if (self->next_token == 0 || IOInheritanceGrants__len(&self->grants) == IOInheritanceGrants__max_size)
    {
        return IOReply__new(IO_REGISTER_FAILED, 0);
    }

    IOInheritanceGrant grant = IOInheritanceGrant__new(self->next_token++, handle, flags, session, pid);
    match_result(
        IOInheritanceGrants__push(&self->grants, grant),
        success(code)
        {
            IOReply reply = IOReply__new(IO_OK, grant.token);
            reply.inheritance_flags = flags;
            return reply;
        },
        error(code, message)
        {
            return IOReply__new(IO_REGISTER_FAILED, 0);
        }
    );
}

IOStatus IOInheritanceRegistry__commit(IOInheritanceRegistryPtr self, IORequestPtr request)
{
    match_optional(
        IOInheritanceRegistry__find(self, request->inheritance_token),
        some(grant)
        {
            IOResourceOwner caller = IOResourceOwner__new(request->client_session, request->client_pid);
            if (!IOResourceOwner__equals(&grant->source, &caller) || grant->target_pid != 0 ||
                request->inheritance_pid == 0)
            {
                return IO_PERMISSION_DENIED;
            }
            grant->target_pid = request->inheritance_pid;
            return IO_OK;
        },
        none()
        {
            return IO_UNKNOWN_HANDLE;
        }
    );
}

Optional(IOInheritanceGrantPtr) IOInheritanceRegistry__claim(IOInheritanceRegistryPtr self, IORequestPtr request)
{
    match_optional(
        IOInheritanceRegistry__find(self, request->inheritance_token),
        some(grant)
        {
            if (grant->target_pid == request->client_pid && grant->target_pid != 0)
            {
                return Some(IOInheritanceGrantPtr, grant);
            }
        },
        none_do_nothing()
    );
    return None(IOInheritanceGrantPtr);
}

IOStatus IOInheritanceRegistry__abort(IOInheritanceRegistryPtr self, IORequestPtr request)
{
    match_optional(
        IOInheritanceRegistry__find(self, request->inheritance_token),
        some(grant)
        {
            IOResourceOwner caller = IOResourceOwner__new(request->client_session, request->client_pid);
            if (!IOResourceOwner__equals(&grant->source, &caller))
            {
                return IO_PERMISSION_DENIED;
            }
            IOInheritanceGrant removed = *grant;
            match_result(
                IOInheritanceGrants__remove(&self->grants, removed),
                success(code)
                {
                    return IO_OK;
                },
                error(code, message)
                {
                    return IO_UNKNOWN_HANDLE;
                }
            );
        },
        none()
        {
            return IO_UNKNOWN_HANDLE;
        }
    );
}

void IOInheritanceRegistry__complete(IOInheritanceRegistryPtr self, IOInheritanceGrantPtr grant)
{
    IOInheritanceGrants__remove(&self->grants, *grant);
}

static bool IOInheritanceGrant__retains_handle(IOInheritanceGrantPtr self, void * context)
{
    return self->target_pid != 0 && self->handle == *(IOHandlePtr)context;
}

bool IOInheritanceRegistry__retains_handle(IOInheritanceRegistryPtr self, IOHandle handle)
{
    return is_something(IOInheritanceGrants__find(&self->grants, IOInheritanceGrant__retains_handle, &handle));
}

static bool IOInheritanceGrant__uncommitted_source(IOInheritanceGrantPtr self, void * context)
{
    return self->target_pid == 0 && IOResourceOwner__equals(&self->source, context);
}

void IOInheritanceRegistry__release(IOInheritanceRegistryPtr self, QueueConnectionID session, uint64_t pid)
{
    IOResourceOwner owner = IOResourceOwner__new(session, pid);
    while (true)
    {
        match_find_result(
            IOInheritanceGrants__find(&self->grants, IOInheritanceGrant__uncommitted_source, &owner),
            some(grant)
            {
                IOInheritanceGrants__remove(&self->grants, grant);
            },
            none()
            {
                return;
            }
        );
    }
}
