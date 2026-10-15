#ifndef HUND_CHANNEL_PROTOCOL_H
#define HUND_CHANNEL_PROTOCOL_H

#include "queue.h"
#define CHANNEL_PAYLOAD_CAPACITY QUEUE_DATA_CAPACITY
#define CHANNEL_REQUEST_OPEN 1ULL
#define CHANNEL_REQUEST_SUBMIT 3ULL
#define CHANNEL_RESPONSE_OPEN 4ULL
#define CHANNEL_RESPONSE_REPLIED 5ULL
#define CHANNEL_RESPONSE_FAILED 6ULL
typedef uint8_t * ChannelDataPtr;
typedef const uint8_t * ConstChannelDataPtr;

// Ownership and length travel on queues, not in writable payload metadata.
static inline QueueMessage ChannelControlMessage__new(uint64_t kind, uint64_t address, uint64_t length, uint64_t token)
{
    QueueMessage message = QueueMessage__new(0, kind);
    message.arg0 = address;
    message.arg1 = length;
    message.arg2 = token;
    return message;
}

static inline RequestTicket ChannelControlMessage__ticket(ConstQueueMessagePtr self)
{
    return RequestTicket__new_with_metadata(self->arg2, &self->request);
}

#endif
