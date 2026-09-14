#ifndef HUND_USER_CHANNEL_SERVER_H
#define HUND_USER_CHANNEL_SERVER_H

#include "channel.h"

typedef struct ChannelServerRequest
{
    size_t size;
    QueueParticipantID client_pid;
} ChannelServerRequest;
typedef ChannelServerRequest * ChannelServerRequestPtr;
OptionalDef(ChannelServerRequest);
typedef Optional(ChannelServerRequest) OptionalChannelServerRequest;
ResultDef(OptionalChannelServerRequest);

typedef struct ChannelServer
{
    Queue control;
    ChannelPayloadPtr payload;
    QueueConnectionID token;
    QueueParticipantID client_pid;
    size_t request_size;
    bool handling_request;
    bool closed;
} ChannelServer;
typedef ChannelServer * ChannelServerPtr;
ResultDef(ChannelServer);

Result(ChannelServer) ChannelServer__accept(QueueConnectionID connection, QueueParticipantID warden_pid);
QueueConnectionID ChannelServer__connection(ChannelServerPtr self);
QueueParticipantID ChannelServer__client_pid(ChannelServerPtr self);
QueueCode ChannelServer__status(ChannelServerPtr self);
Result(OptionalChannelServerRequest) ChannelServer__poll(ChannelServerPtr self);
bool ChannelServer__read_request(ChannelServerPtr self, ChannelDataPtr request, size_t size);
Result(ChannelCode) ChannelServer__reply(ChannelServerPtr self, ConstChannelDataPtr reply, size_t size);
ChannelCode ChannelServer__close(ChannelServerPtr self);

#endif
