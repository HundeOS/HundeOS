#include "request_graph.h"
#include "ipc/channel_protocol.h"
#include "../debug.h"

FixedArrayBodyDefine(RequestLinks);
OptionalDef(QueueMessage);

bool RequestLink__equals(void * first, void * second)
{
    ConstRequestLinkPtr a = first;
    ConstRequestLinkPtr b = second;
    return RequestTicket__equals(&a->ticket, &b->ticket);
}

RequestGraph RequestGraph__new(void)
{
    return (RequestGraph){.links = RequestLinks__new()};
}

static RequestLink RequestLink__new(RequestTicket ticket, ProcessPtr caller, ProcessPtr responder, ProcessPtr relay,
                                    QueueConnectionID route, bool replied)
{
    return (RequestLink){
        .ticket = ticket, .caller = caller, .responder = responder, .relay = relay, .route = route, .replied = replied};
}

static Optional(QueueMessage) QueueConnection__publication(QueueConnectionPtr self, bool client)
{
    if (!self->memory.valid)
    {
        return None(QueueMessage);
    }
    Address base = self->memory.kernel_addr.virtual_addr;
    QueueIdentityPtr identity = (QueueIdentityPtr)base;
    if (__atomic_load_n(&identity->closed, __ATOMIC_ACQUIRE) != 0 ||
        __atomic_load_n(&identity->accepted, __ATOMIC_ACQUIRE) == 0)
    {
        return None(QueueMessage);
    }
    QueuePagePtr page = (QueuePagePtr)(base + (client ? 1 : 2) * QUEUE_PAGE_SIZE);
    match_result(
        QueuePublications__at(&page->publication, 0),
        success(publication)
        {
            if (publication.cursor != 0 && publication.cursor == __atomic_load_n(&page->produced, __ATOMIC_ACQUIRE))
            {
                return Some(QueueMessage, publication.message);
            }
        },
        error(code, message)
        {
            return None(QueueMessage);
        }
    );
    return None(QueueMessage);
}

static Optional(RequestLink) QueueConnection__request_link(QueueConnectionPtr self, QueuesPtr queues)
{
    QueueMessage forwarded;
    match_optional(
        QueueConnection__publication(self, true),
        some(message)
        {
            forwarded = message;
        },
        none()
        {
            return None(RequestLink);
        }
    );
    RequestTicket ticket = ChannelControlMessage__ticket(&forwarded);
    if (forwarded.kind != CHANNEL_REQUEST_SERVE || !RequestTicket__valid(&ticket))
    {
        return None(RequestLink);
    }
    QueueConnectionPtr source;
    match_optional(
        QueueConnections__ptr_at(&queues->connections, ticket.connection % QUEUE_LIMIT),
        some(connection)
        {
            source = connection;
        },
        none()
        {
            return None(RequestLink);
        }
    );
    if (source->connection != ticket.connection || source->connection == self->connection)
    {
        return None(RequestLink);
    }
    ProcessPtr caller;
    ProcessPtr relay;
    ProcessPtr responder;
    match_optional(
        source->client,
        some(value)
        {
            caller = value;
        },
        none()
        {
            match_optional(
                source->kernel_origin,
                some(value)
                {
                    caller = value;
                },
                none()
                {
                    return None(RequestLink);
                }
            );
        }
    );
    match_optional(source->server,
        some(value)
        {
            relay = value;
        },
        none()
        {
            return None(RequestLink);
        }
    );
    match_optional(self->server,
        some(value)
        {
            responder = value;
        },
        none()
        {
            return None(RequestLink);
        }
    );
    match_optional(
        self->client,
        some(value)
        {
            if (value != relay)
            {
                return None(RequestLink);
            }
        },
        none()
        {
            return None(RequestLink);
        }
    );
    QueueParticipantID routed_client = source->kernel_client ? QUEUE_KERNEL_PID : caller->pid;
    if (forwarded.arg3 != routed_client)
    {
        return None(RequestLink);
    }
    match_optional(
        QueueConnection__publication(source, true),
        some(message)
        {
            RequestTicket submitted = ChannelControlMessage__ticket(&message);
            if (message.kind != CHANNEL_REQUEST_SUBMIT || !RequestTicket__equals(&ticket, &submitted))
            {
                return None(RequestLink);
            }
        },
        none()
        {
            return None(RequestLink);
        }
    );
    bool replied = false;
    match_optional(
        QueueConnection__publication(self, false),
        some(message)
        {
            RequestTicket completed = ChannelControlMessage__ticket(&message);
            replied = message.kind == CHANNEL_RESPONSE_REPLIED && RequestTicket__equals(&ticket, &completed);
        },
        none_do_nothing()
    );
    return Some(RequestLink, RequestLink__new(ticket, caller, responder, relay, self->connection, replied));
}

void RequestGraph__refresh(RequestGraphPtr self, QueuesPtr queues)
{
    while (RequestLinks__len(&self->links) != 0)
    {
        match_optional(RequestLinks__pop(&self->links),
            some_do_nothing(),
            none_do_nothing()
        );
    }
    for_each_fa(
        QueueConnections,
        queues->connections,
        index,
        connection,
        {
            match_optional(
                QueueConnection__request_link(&connection, queues),
                some(link)
                {
                    match_result(
                        RequestLinks__push(&self->links, link),
                        success_do_nothing(),
                        error(code, message)
                        {
                            __d_printf(SCHEDULER, "Request graph capacity exceeded: %i\n", code);
                            return;
                        }
                    );
                },
                none_do_nothing()
            );
        }
    );
}

static bool RequestLink__matches(RequestLinkPtr self, void * context)
{
    ConstRequestTicketPtr ticket = context;
    return self->ticket.connection == ticket->connection &&
           self->ticket.metadata.generation == ticket->metadata.generation;
}

Optional(RequestLink) RequestGraph__find(RequestGraphPtr self, uint64_t connection, uint64_t generation)
{
    RequestTicket key = RequestTicket__new(connection, generation, 0, &None(RequestTicket));
    match_find_result(
        RequestLinks__find(&self->links, RequestLink__matches, &key),
        some(link)
        {
            return Some(RequestLink, link);
        },
        none_do_nothing()
    );
    return None(RequestLink);
}

bool RequestGraph__valid(RequestGraphPtr self, ConstRequestLinkPtr link)
{
    RequestLink current = *link;
    // A valid ancestry reaches a root within the number of live links; cycles cannot.
    for (uint64_t depth = 0; depth < RequestLinks__len(&self->links); depth++)
    {
        if (current.ticket.metadata.parent_connection == 0)
        {
            return current.ticket.metadata.origin_pid == current.caller->pid;
        }
        match_optional(
            RequestGraph__find(self, current.ticket.metadata.parent_connection,
                               current.ticket.metadata.parent_generation),
            some(parent)
            {
                if (parent.responder != current.caller || parent.replied ||
                    parent.ticket.metadata.origin_pid != current.ticket.metadata.origin_pid)
                {
                    return false;
                }
                current = parent;
            },
            none()
            {
                return false;
            }
        );
    }
    return false;
}
