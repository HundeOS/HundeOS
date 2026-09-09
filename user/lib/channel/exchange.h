#ifndef __USER_CHANNEL_EXCHANGE_H
#define __USER_CHANNEL_EXCHANGE_H

#include "sys/atomic_message_queue.h"

#define CHANNEL_EXCHANGE_CAPACITY 1024U
#define CHANNEL_REQUEST_OPEN 1ULL
#define CHANNEL_REQUEST_SERVE 2ULL
#define CHANNEL_REQUEST_SUBMIT 3ULL
#define CHANNEL_RESPONSE_OPEN 4ULL
#define CHANNEL_RESPONSE_REPLIED 5ULL
#define CHANNEL_RESPONSE_FAILED 6ULL

void ChannelExchange__prepare_wait(void);
void ChannelExchange__wait_event(void);

typedef struct ChannelPayload
{
    uint8_t request[CHANNEL_EXCHANGE_CAPACITY];
    uint8_t reply[CHANNEL_EXCHANGE_CAPACITY];
} ChannelPayload;
typedef ChannelPayload * ChannelPayloadPtr;
_Static_assert(sizeof(ChannelPayload) <= HUND_SERVICE_PAGE_SIZE, "Channel payload exceeds one page");

// Ownership and length travel on atomic message queues, not in writable payload metadata.
static inline ServiceQueueMessage ChannelExchange__message(uint64_t kind, uint64_t address, uint64_t length,
                                                           uint64_t token)
{
    ServiceQueueMessage message = ServiceQueueMessage__new(0, kind);
    message.arg0 = address;
    message.arg1 = length;
    message.arg2 = token;
    return message;
}

#endif
