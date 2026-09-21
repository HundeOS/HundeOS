#ifndef HUND_CHANNEL_PROTOCOL_H
#define HUND_CHANNEL_PROTOCOL_H

#include "queue.h"
#include "syscalls/mem.h"

#define CHANNEL_PAYLOAD_CAPACITY 1024U
#define CHANNEL_REQUEST_OPEN 1ULL
#define CHANNEL_REQUEST_SERVE 2ULL
#define CHANNEL_REQUEST_SUBMIT 3ULL
#define CHANNEL_RESPONSE_OPEN 4ULL
#define CHANNEL_RESPONSE_REPLIED 5ULL
#define CHANNEL_RESPONSE_FAILED 6ULL
#define CHANNEL_REQUEST_ACKNOWLEDGE 7ULL
#define CHANNEL_REQUEST_CANCEL 8ULL

typedef struct ChannelPayload
{
    uint8_t request[CHANNEL_PAYLOAD_CAPACITY];
    uint8_t reply[CHANNEL_PAYLOAD_CAPACITY];
} ChannelPayload;
typedef ChannelPayload * ChannelPayloadPtr;
typedef uint8_t * ChannelDataPtr;
typedef const uint8_t * ConstChannelDataPtr;
_Static_assert(sizeof(ChannelPayload) <= MEM_SHARED_PAGE_SIZE, "Channel payload exceeds one page");

bool ChannelPayload__write_request(ChannelPayloadPtr self, ConstChannelDataPtr data, size_t size);
bool ChannelPayload__read_request(ChannelPayloadPtr self, ChannelDataPtr data, size_t size);
bool ChannelPayload__write_reply(ChannelPayloadPtr self, ConstChannelDataPtr data, size_t size);
bool ChannelPayload__read_reply(ChannelPayloadPtr self, ChannelDataPtr data, size_t size);

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
