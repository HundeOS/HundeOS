#ifndef HUND_REQUEST_TICKET_H
#define HUND_REQUEST_TICKET_H

#include <stdint.h>
#include "../types/optional.h"

typedef struct RequestMetadata
{
    uint64_t generation;
    uint64_t origin_pid;
    uint64_t parent_connection;
    uint64_t parent_generation;
} RequestMetadata;

typedef const RequestMetadata * ConstRequestMetadataPtr;

typedef struct RequestTicket
{
    uint64_t connection;
    RequestMetadata metadata;
} RequestTicket;

typedef const RequestTicket * ConstRequestTicketPtr;
OptionalDef(RequestTicket);

RequestTicket RequestTicket__new(uint64_t connection, uint64_t generation, uint64_t caller,
                                 const OptionalPtr(RequestTicket) parent);
RequestTicket RequestTicket__new_with_metadata(uint64_t connection, ConstRequestMetadataPtr metadata);
bool RequestTicket__equals(ConstRequestTicketPtr self, ConstRequestTicketPtr other);
bool RequestTicket__valid(ConstRequestTicketPtr self);

// Userspace execution context. Fibre switches save and restore this value.
Optional(RequestTicket) ChannelRequestContext__current(void);
Optional(RequestTicket) ChannelRequestContext__swap(const OptionalPtr(RequestTicket) next);

#endif
