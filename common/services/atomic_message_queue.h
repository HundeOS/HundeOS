#ifndef HUND_ATOMIC_MESSAGE_QUEUE_H
#define HUND_ATOMIC_MESSAGE_QUEUE_H

#include "atomic_message_queue_layout.h"
#include "../types/result.h"

// Private per-endpoint cursors must not be reconstructed from peer-writable state.
typedef struct AtomicMessageQueue
{
    AtomicMessageQueueIdentityPtr identity;
    AtomicMessageQueuePagePtr outgoing;
    AtomicMessageQueuePagePtr incoming;
    AtomicMessageQueueConnectionID connection;
    AtomicMessageQueueParticipantID peer_pid;
    AtomicMessageQueueCursor read;
    AtomicMessageQueueCursor written;
    AtomicMessageQueueCursor acknowledged;
} AtomicMessageQueue;
typedef AtomicMessageQueue * AtomicMessageQueuePtr;
ResultDef(AtomicMessageQueueCode);
ResultDef(AtomicMessageQueue);
ResultDef(ServiceQueueMessage);

AtomicMessageQueue AtomicMessageQueue__new(AtomicMessageQueueIdentityPtr identity, AtomicMessageQueuePagePtr outgoing,
                                           AtomicMessageQueuePagePtr incoming,
                                           AtomicMessageQueueConnectionID connection,
                                           AtomicMessageQueueParticipantID peer_pid);
Result(AtomicMessageQueue)
    AtomicMessageQueue__attach(AtomicMessageQueueIdentityPtr identity, AtomicMessageQueuePagePtr client,
                               AtomicMessageQueuePagePtr server, AtomicMessageQueueConnectionID connection,
                               AtomicMessageQueueParticipantID pid);
AtomicMessageQueueCode AtomicMessageQueue__status(AtomicMessageQueuePtr self);
Result(AtomicMessageQueueCode)
    AtomicMessageQueue__push(AtomicMessageQueuePtr self, ConstServiceQueueMessagePtr message);
Result(ServiceQueueMessage) AtomicMessageQueue__pop(AtomicMessageQueuePtr self);

#endif
