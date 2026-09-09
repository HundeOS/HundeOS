#ifndef __HUNDEKANAL_WARTER_LIFECYCLE_H
#define __HUNDEKANAL_WARTER_LIFECYCLE_H

#include "channel/exchange.h"
#include "types/fixed_array.h"

OptionalDef(AtomicMessageQueue);
OptionalDef(ServiceQueueMessage);

typedef struct ChannelExchange ChannelExchange;
typedef ChannelExchange * ChannelExchangePtr;

typedef struct ChannelExchangeState ChannelExchangeState;
typedef const ChannelExchangeState * ConstChannelExchangeStatePtr;
ResultDef(ConstChannelExchangeStatePtr);

typedef Result(ConstChannelExchangeStatePtr) (*ChannelExchangeStateAction)(ChannelExchangePtr self);

struct ChannelExchangeState
{
    ChannelExchangeStateAction action;
};

// Warden-private ownership and mapping records, never writable by endpoints.
struct ChannelExchange
{
    AtomicMessageQueue client;
    Optional(AtomicMessageQueue) server;
    Optional(ServiceQueueMessage) reply;
    ConstChannelExchangeStatePtr state;
    uint64_t address;
    uint64_t length;
    bool client_closed;
};
OptionalDef(ChannelExchange);

extern const ChannelExchangeState CHANNEL_EXCHANGE_WAIT_OPEN;
extern const ChannelExchangeState CHANNEL_EXCHANGE_CONNECTING;
extern const ChannelExchangeState CHANNEL_EXCHANGE_DELIVERING_OPEN;
extern const ChannelExchangeState CHANNEL_EXCHANGE_OPEN;
extern const ChannelExchangeState CHANNEL_EXCHANGE_SUBMITTING;
extern const ChannelExchangeState CHANNEL_EXCHANGE_WORKING;
extern const ChannelExchangeState CHANNEL_EXCHANGE_WORKING_CLIENT_CLOSED;
extern const ChannelExchangeState CHANNEL_EXCHANGE_CLOSING_SERVER;
extern const ChannelExchangeState CHANNEL_EXCHANGE_DELIVERING_REPLY;
extern const ChannelExchangeState CHANNEL_EXCHANGE_WAIT_CLIENT_CLOSE;
extern const ChannelExchangeState CHANNEL_EXCHANGE_RELEASING;
extern const ChannelExchangeState CHANNEL_EXCHANGE_COMPLETE;

ChannelExchange ChannelExchange__new(AtomicMessageQueuePtr client);
ConstChannelExchangeStatePtr ChannelExchange__state(ChannelExchangePtr self);
bool ChannelExchange__step(ChannelExchangePtr self);
bool ChannelExchange__equals(void * first, void * second);

FixedArrayDefine(ChannelExchange, ATOMIC_MESSAGE_QUEUE_LIMIT, ChannelExchanges, &ChannelExchange__equals);
typedef ChannelExchanges * ChannelExchangesPtr;

bool ChannelExchanges__contains(ChannelExchangesPtr self, AtomicMessageQueueConnectionID connection);
bool ChannelExchanges__step(ChannelExchangesPtr self, AtomicMessageQueueArrivalsPtr arrivals);

#endif
