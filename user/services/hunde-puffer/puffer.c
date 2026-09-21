#include "puffer.h"
FixedArrayBodyDefine(BufferLeases);
bool BufferLease__equals(void * first, void * second)
{
    return ((BufferLeasePtr)first)->id == ((BufferLeasePtr)second)->id;
}
Puffer Puffer__new(ServiceDirectoryPtr directory)
{
    return (Puffer){.leases = BufferLeases__new(), .directory = directory};
}
static BufferLease BufferLease__new(BufferID id, BufferParticipant server, BufferParticipant client)
{
    return (BufferLease){.id = id, .server = server, .client = client, .pooled = server == 0 && client == 0};
}
static bool BufferLease__matches(BufferLeasePtr self, void * context)
{
    return self->id == *(BufferIDPtr)context;
}
static bool BufferLease__pooled(BufferLeasePtr self, void * context)
{
    return self->pooled;
}

void Puffer__reconcile(PufferPtr self, BufferID id)
{
    int64_t state = BufferMemory__operation(MEM_BUFFER_STATE, id, 0, 0, 0);
    if (state < 0 || (state & (BUFFER_CLIENT_ATTACHED | BUFFER_SERVER_ATTACHED | BUFFER_RETAINED)) != 0)
    {
        return;
    }
    match_find_result_ptr(
        BufferLeases__find(&self->leases, BufferLease__matches, &id),
        some(lease)
        {
            lease->pooled = true;
        },
        none()
        {
            BufferLeases__push(&self->leases, BufferLease__new(id, 0, 0));
        }
    );
}

void Puffer__housekeeping(PufferPtr self)
{
    unsigned pooled = 0;
    for (ArrayIndex index = 0; index < BufferLeases__len(&self->leases);)
    {
        match_optional(
            BufferLeases__at(&self->leases, index),
            some(lease)
            {
                if (lease.pooled && ++pooled > BUFFER_POOL_LIMIT &&
                    BufferMemory__operation(MEM_BUFFER_FREE, lease.id, 0, 0, 0) == BUFFER_OK)
                {
                    BufferLeases__remove(&self->leases, lease);
                    continue;
                }
            },
            none_do_nothing()
        );
        index++;
    }
}

static BufferStatus Puffer__release(PufferPtr self, BufferParticipant caller, BufferID id, BufferParticipant client,
                                    bool pair)
{
    if (id == 0)
    {
        return BUFFER_OK;
    }
    match_find_result_ptr(
        BufferLeases__find(&self->leases, BufferLease__matches, &id),
        some(lease)
        {
            if (caller != lease->client && caller != lease->server)
            {
                return BUFFER_DENIED;
            }
            if (pair && (caller != lease->server || client != lease->client))
            {
                return BUFFER_DENIED;
            }
            int64_t result = BufferMemory__operation(MEM_BUFFER_DROP, id, caller, 0, 0);
            if (result != BUFFER_OK && result != BUFFER_DENIED)
            {
                return result;
            }
            if (pair)
            {
                BufferMemory__operation(MEM_BUFFER_DROP, id, lease->client, 0, 0);
            }
            Puffer__reconcile(self, id);
            return BUFFER_OK;
        },
        none()
        {
            return BUFFER_OK;
        }
    );
}

BufferReply Puffer__execute(PufferPtr self, BufferParticipant caller, ConstBufferRequestPtr request)
{
    if (request->operation == BUFFER_RELEASE || request->operation == BUFFER_RELEASE_PAIR)
    {
        bool pair = request->operation == BUFFER_RELEASE_PAIR;
        BufferStatus status = Puffer__release(self, caller, request->id, request->client, pair);
        if (status == BUFFER_OK && pair)
        {
            status = Puffer__release(self, caller, request->second, request->client, pair);
        }
        Puffer__housekeeping(self);
        return BufferReply__new(status);
    }
    if (request->operation != BUFFER_ACQUIRE)
    {
        return BufferReply__new(BUFFER_INVALID);
    }
    BString service = BufferRequest__service(request);
    uint64_t server = unwrap_optional(ServiceDirectory__find_service_pid(self->directory, &service), 0);
    if (caller == 0 || server != caller)
    {
        return BufferReply__new(BUFFER_DENIED);
    }
    if (BufferLeases__len(&self->leases) >= BUFFER_LIMIT)
    {
        Puffer__housekeeping(self);
    }
    BufferID reuse = 0;
    match_find_result(
        BufferLeases__find(&self->leases, BufferLease__pooled, self),
        some(lease)
        {
            reuse = lease.id;
        },
        none_do_nothing()
    );
    if (reuse == 0 && BufferLeases__len(&self->leases) >= BUFFER_LIMIT)
    {
        return BufferReply__new(BUFFER_NO_MEMORY);
    }
    int64_t result = BufferMemory__operation(MEM_BUFFER_CREATE, caller, request->client, request->direction, reuse);
    if (result < 0)
    {
        return BufferReply__new(result);
    }
    BufferLease created = BufferLease__new(result, caller, request->client);
    created.service = service;
    if (reuse != 0)
    {
        match_find_result_ptr(
            BufferLeases__find(&self->leases, BufferLease__matches, &reuse),
            some(lease)
            {
                *lease = created;
            },
            none_do_nothing()
        );
    }
    else
    {
        BufferLeases__push(&self->leases, created);
    }
    BufferReply reply = BufferReply__new(BUFFER_OK);
    reply.buffer = BufferDescriptor__new(result, request->direction);
    return reply;
}
