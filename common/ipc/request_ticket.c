#include "request_ticket.h"

static Optional(RequestTicket) current_request = None(RequestTicket);

RequestTicket RequestTicket__new(uint64_t connection, uint64_t generation, uint64_t caller,
                                 const OptionalPtr(RequestTicket) parent)
{
    RequestTicket ticket = {.connection = connection, .metadata = {.generation = generation, .origin_pid = caller}};
    match_optional(
        *parent,
        some(value)
        {
            ticket.metadata.origin_pid = value.metadata.origin_pid;
            ticket.metadata.parent_connection = value.connection;
            ticket.metadata.parent_generation = value.metadata.generation;
        },
        none_do_nothing()
    );
    return ticket;
}

RequestTicket RequestTicket__new_with_metadata(uint64_t connection, ConstRequestMetadataPtr metadata)
{
    return (RequestTicket){.connection = connection, .metadata = *metadata};
}

bool RequestTicket__equals(ConstRequestTicketPtr self, ConstRequestTicketPtr other)
{
    return self->connection == other->connection && self->metadata.generation == other->metadata.generation &&
           self->metadata.origin_pid == other->metadata.origin_pid &&
           self->metadata.parent_connection == other->metadata.parent_connection &&
           self->metadata.parent_generation == other->metadata.parent_generation;
}

bool RequestTicket__valid(ConstRequestTicketPtr self)
{
    return self->connection != 0 && self->metadata.generation != 0 && self->metadata.origin_pid != 0 &&
           ((self->metadata.parent_connection == 0) == (self->metadata.parent_generation == 0));
}

Optional(RequestTicket) ChannelRequestContext__current(void)
{
    return current_request;
}

Optional(RequestTicket) ChannelRequestContext__swap(const OptionalPtr(RequestTicket) next)
{
    Optional(RequestTicket) previous = current_request;
    current_request = *next;
    return previous;
}
