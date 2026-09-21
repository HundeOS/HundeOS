#ifndef HUND_CHANNEL_H
#define HUND_CHANNEL_H

#include "channel_protocol.h"

#define CHANNEL_WARDEN_SERVICE_NAME "hundekanal-warter"

typedef enum ChannelCode
{
    CHANNEL_OK = 0,
    CHANNEL_PENDING = 1,
    CHANNEL_UNAVAILABLE = -1,
    CHANNEL_TRANSPORT_FAILED = -2,
    CHANNEL_INVALID_REPLY = -3,
    CHANNEL_PAYLOAD_TOO_LARGE = -4,
    CHANNEL_BUSY = -5,
} ChannelCode;
ResultDef(ChannelCode);

typedef uint64_t ChannelReplySize;
ResultDef(ChannelReplySize);
OptionalDef(ChannelReplySize);
typedef Optional(ChannelReplySize) OptionalChannelReplySize;
ResultDef(OptionalChannelReplySize);

typedef struct Channel
{
    Queue control;
    ChannelPayloadPtr payload;
    QueueConnectionID token;
    QueueParticipantID server_pid;
    QueueParticipantID origin_pid;
    bool request_pending;
    RequestTicket ticket;
    Optional(ChannelReplySize) received_reply;
} Channel;
typedef Channel * ChannelPtr;
ResultDef(Channel);

// Queue and payload mappings must remain valid throughout every call.
Channel Channel__new(QueuePtr control, ChannelPayloadPtr payload);
Channel Channel__new_with_origin(QueuePtr control, ChannelPayloadPtr payload, QueueParticipantID origin_pid);
QueueParticipantID Channel__server_pid(ChannelPtr self);
Result(ChannelCode) Channel__try_request(ChannelPtr self, ConstChannelDataPtr request, size_t request_size);
Result(ChannelCode) Channel__try_request_with_parent(ChannelPtr self, ConstChannelDataPtr request, size_t request_size,
                                                     const OptionalPtr(RequestTicket) parent);
Result(OptionalChannelReplySize) Channel__try_poll_reply(ChannelPtr self, ChannelDataPtr reply, size_t reply_capacity);
#endif
