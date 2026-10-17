#include "request_graph.h"
#include "ipc/channel_protocol.h"
#include "../debug.h"

FixedArrayBodyDefine(RequestLinks);
OptionalDef(QueuePublication);

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

static RequestLink RequestLink__new(RequestTicket ticket, ProcessPtr caller, ProcessPtr responder,
                                    QueueConnectionID route, bool replied)
{
    return (RequestLink){
        .ticket = ticket, .caller = caller, .responder = responder, .route = route, .replied = replied};
}

static Optional(QueuePublication) QueueConnection__publication(QueueConnectionPtr self, bool client)
{
    if (!self->memory.valid)
    {
        return None(QueuePublication);
    }
    Address base = u_kvaddr(self->memory);
    QueueIdentityPtr identity = (QueueIdentityPtr)base;
    if (__atomic_load_n(&identity->closed, __ATOMIC_ACQUIRE) != 0 ||
        __atomic_load_n(&identity->accepted, __ATOMIC_ACQUIRE) == 0)
    {
        return None(QueuePublication);
    }
    QueuePagePtr page = (QueuePagePtr)(base + (client ? 1 : 2) * QUEUE_PAGE_SIZE);
    match_result(
        QueuePublications__at(&page->publication, 0),
        success(publication)
        {
            if (publication.cursor != 0 && publication.cursor == __atomic_load_n(&page->produced, __ATOMIC_ACQUIRE))
            {
                return Some(QueuePublication, publication);
            }
        },
        error(code, message)
        {
            return None(QueuePublication);
        }
    );
    return None(QueuePublication);
}

static Optional(RequestLink) QueueConnection__request_link(QueueConnectionPtr self)
{
    QueueMessage submitted;
    match_optional(
        QueueConnection__publication(self, true),
        some(publication)
        {
            submitted = publication.message;
        },
        none()
        {
            return None(RequestLink);
        }
    );
    RequestTicket ticket = ChannelControlMessage__ticket(&submitted);
    if (submitted.kind != CHANNEL_REQUEST_SUBMIT || ticket.connection != self->connection ||
        !RequestTicket__valid(&ticket))
    {
        return None(RequestLink);
    }
    ProcessPtr caller;
    ProcessPtr responder;
    match_optional(
        self->client,
        some(value)
        {
            caller = value;
        },
        none()
        {
            match_optional(self->kernel_origin,
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
    bool replied = false;
    match_optional(
        QueueConnection__publication(self, false),
        some(publication)
        {
            RequestTicket completed = ChannelControlMessage__ticket(&publication.message);
            replied =
                publication.message.kind == CHANNEL_RESPONSE_REPLIED && RequestTicket__equals(&ticket, &completed);
            QueuePagePtr client_page = (QueuePagePtr)(u_kvaddr(self->memory) + QUEUE_PAGE_SIZE);
            if (replied && publication.cursor == __atomic_load_n(&client_page->consumed, __ATOMIC_ACQUIRE))
            {
                return None(RequestLink);
            }
        },
        none_do_nothing()
    );
    return Some(RequestLink, RequestLink__new(ticket, caller, responder, self->connection, replied));
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
                QueueConnection__request_link(&connection),
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

uint64_t RequestGraph__dependency_weight(RequestGraphPtr self, ProcessPtr process)
{
    uint64_t weight = 0;
    for_each_fa(
        RequestLinks,
        self->links,
        index,
        link,
        {
            if (link.responder != process || link.replied || !RequestGraph__valid(self, &link))
            {
                continue;
            }
            RequestLink current = link;
            for (uint64_t depth = 0; depth < RequestLinks__len(&self->links); depth++)
            {
                weight++;
                if (current.ticket.metadata.parent_connection == 0)
                {
                    break;
                }
                match_optional(
                    RequestGraph__find(self, current.ticket.metadata.parent_connection,
                                       current.ticket.metadata.parent_generation),
                    some(parent)
                    {
                        current = parent;
                    },
                    none()
                    {
                        break;
                    }
                );
            }
        }
    );
    return weight;
}
