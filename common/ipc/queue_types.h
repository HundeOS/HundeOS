#ifndef HUND_QUEUE_TYPES_H
#define HUND_QUEUE_TYPES_H

#include <stdint.h>

#include "../types/optional.h"
#include "../types/result.h"
#include "request_ticket.h"

typedef uint64_t QueueHandleID;
typedef QueueHandleID QueueConnectionID;
typedef QueueConnectionID * QueueConnectionIDPtr;
typedef QueueHandleID QueueEndpointID;
typedef uint64_t QueueParticipantID;
#define QUEUE_KERNEL_PID UINT64_MAX
typedef uint64_t QueueEpoch;
typedef uint64_t QueueCursor;
typedef uint64_t QueueMessageCount;
typedef uint64_t QueueSlotIndex;
typedef uint64_t QueueStateFlag;
typedef uint64_t QueueHandleGeneration;

#define QUEUE_MESSAGE_PAYLOAD_SIZE 32U
_Static_assert(sizeof(RequestMetadata) == QUEUE_MESSAGE_PAYLOAD_SIZE, "Request metadata must fit the control payload");

typedef struct QueueMessage
{
    uint64_t sender_pid;
    uint64_t kind;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    union
    {
        uint8_t payload[QUEUE_MESSAGE_PAYLOAD_SIZE];
        RequestMetadata request;
    };
} QueueMessage;
typedef QueueMessage * QueueMessagePtr;
typedef const QueueMessage * ConstQueueMessagePtr;

static inline QueueMessage QueueMessage__new(uint64_t sender_pid, uint64_t kind)
{
    return (QueueMessage){.sender_pid = sender_pid, .kind = kind};
}

OptionalDef(QueueConnectionID);
OptionalDef(QueueEndpointID);
ResultDef(QueueConnectionID);
ResultDef(QueueHandleID);

#endif
