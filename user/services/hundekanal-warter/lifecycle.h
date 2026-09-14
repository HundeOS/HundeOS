#ifndef __HUNDEKANAL_WARTER_LIFECYCLE_H
#define __HUNDEKANAL_WARTER_LIFECYCLE_H

#include "channel/protocol.h"
#include "types/fixed_array.h"

OptionalDef(Queue);
OptionalDef(QueueMessage);

typedef struct ChannelSession ChannelSession;
typedef ChannelSession * ChannelSessionPtr;

typedef struct ChannelSessionState ChannelSessionState;
typedef const ChannelSessionState * ConstChannelSessionStatePtr;
ResultDef(ConstChannelSessionStatePtr);

typedef Result(ConstChannelSessionStatePtr) (*ChannelSessionStateAction)(ChannelSessionPtr self);

struct ChannelSessionState
{
    ChannelSessionStateAction action;
};

// Warden-private ownership and mapping records, never writable by endpoints.
struct ChannelSession
{
    Queue client;
    Optional(Queue) server;
    Optional(QueueMessage) reply;
    ConstChannelSessionStatePtr state;
    uint64_t address;
    uint64_t length;
    bool client_closed;
};
OptionalDef(ChannelSession);

extern const ChannelSessionState CHANNEL_SESSION_WAIT_OPEN;
extern const ChannelSessionState CHANNEL_SESSION_CONNECTING;
extern const ChannelSessionState CHANNEL_SESSION_DELIVERING_OPEN;
extern const ChannelSessionState CHANNEL_SESSION_OPEN;
extern const ChannelSessionState CHANNEL_SESSION_SUBMITTING;
extern const ChannelSessionState CHANNEL_SESSION_WORKING;
extern const ChannelSessionState CHANNEL_SESSION_CANCELLING;
extern const ChannelSessionState CHANNEL_SESSION_WORKING_CLIENT_CLOSED;
extern const ChannelSessionState CHANNEL_SESSION_CLOSING_SERVER;
extern const ChannelSessionState CHANNEL_SESSION_DELIVERING_REPLY;
extern const ChannelSessionState CHANNEL_SESSION_WAIT_CLIENT_ACKNOWLEDGEMENT;
extern const ChannelSessionState CHANNEL_SESSION_WAIT_CLIENT_CLOSE;
extern const ChannelSessionState CHANNEL_SESSION_RELEASING;
extern const ChannelSessionState CHANNEL_SESSION_COMPLETE;

ChannelSession ChannelSession__new(QueuePtr client);
ConstChannelSessionStatePtr ChannelSession__state(ChannelSessionPtr self);
bool ChannelSession__step(ChannelSessionPtr self);
bool ChannelSession__equals(void * first, void * second);

FixedArrayDefine(ChannelSession, QUEUE_LIMIT, ChannelSessions, &ChannelSession__equals);
typedef ChannelSessions * ChannelSessionsPtr;

bool ChannelSessions__contains(ChannelSessionsPtr self, QueueConnectionID connection);
bool ChannelSessions__step(ChannelSessionsPtr self, QueueArrivalsPtr arrivals);

#endif
