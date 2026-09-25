#ifndef KERNEL_REQUEST_GRAPH_H
#define KERNEL_REQUEST_GRAPH_H

#include "../ipc/queues.h"

typedef struct RequestLink
{
    RequestTicket ticket;
    ProcessPtr caller;
    ProcessPtr responder;
    ProcessPtr relay;
    QueueConnectionID route;
    bool replied;
} RequestLink;

typedef RequestLink * RequestLinkPtr;
typedef const RequestLink * ConstRequestLinkPtr;
OptionalDef(RequestLink);
bool RequestLink__equals(void * first, void * second);
FixedArrayDefine(RequestLink, QUEUE_LIMIT, RequestLinks, RequestLink__equals);

typedef struct RequestGraph
{
    RequestLinks links;
} RequestGraph;

typedef RequestGraph * RequestGraphPtr;

RequestGraph RequestGraph__new(void);
void RequestGraph__refresh(RequestGraphPtr self, QueuesPtr queues);
Optional(RequestLink) RequestGraph__find(RequestGraphPtr self, uint64_t connection, uint64_t generation);
bool RequestGraph__valid(RequestGraphPtr self, ConstRequestLinkPtr link);

#endif
