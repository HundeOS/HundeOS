#ifndef __HUNDEKANAL_WARTER_LIFECYCLE_H
#define __HUNDEKANAL_WARTER_LIFECYCLE_H

#include "channel/protocol.h"
#include "types/fixed_array.h"

OptionalDef(QueueMessage);

typedef enum ChannelSessionState
{
    CHANNEL_SESSION_WAIT_OPEN,
    CHANNEL_SESSION_DELIVERING_OPEN,
    CHANNEL_SESSION_WAIT_CLIENT_CLOSE,
    CHANNEL_SESSION_COMPLETE,
} ChannelSessionState;

typedef struct ChannelSession
{
    Queue client;
    Optional(QueueMessage) reply;
    QueueConnectionID delegated;
    ChannelSessionState state;
    bool client_closed;
} ChannelSession;

typedef ChannelSession * ChannelSessionPtr;
OptionalDef(ChannelSession);

ChannelSession ChannelSession__new(QueuePtr client);
ChannelSessionState ChannelSession__state(ChannelSessionPtr self);
bool ChannelSession__step(ChannelSessionPtr self);
bool ChannelSession__equals(void * first, void * second);

FixedArrayDefine(ChannelSession, QUEUE_LIMIT, ChannelSessions, &ChannelSession__equals);
typedef ChannelSessions * ChannelSessionsPtr;

bool ChannelSessions__contains(ChannelSessionsPtr self, QueueConnectionID connection);
bool ChannelSessions__step(ChannelSessionsPtr self, QueueArrivalsPtr arrivals);

#endif
