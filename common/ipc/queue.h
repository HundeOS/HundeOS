#ifndef HUND_QUEUE_H
#define HUND_QUEUE_H

#include "queue_layout.h"
#include "../types/result.h"

// Private per-endpoint cursors must not be reconstructed from peer-writable state.
typedef struct Queue
{
    QueueIdentityPtr identity;
    QueuePagePtr outgoing;
    QueuePagePtr incoming;
    QueueConnectionID connection;
    QueueParticipantID peer_pid;
    QueueCursor read;
    QueueCursor written;
    QueueCursor acknowledged;
} Queue;
typedef Queue * QueuePtr;
ResultDef(QueueCode);
ResultDef(Queue);
ResultDef(QueueMessage);

typedef struct QueueProgress
{
    QueueCursor incoming_produced;
    QueueCursor incoming_consumed;
    QueueCursor outgoing_produced;
    QueueCursor outgoing_consumed;
    QueueStateFlag accepted;
    QueueStateFlag closed;
} QueueProgress;
typedef const QueueProgress * ConstQueueProgressPtr;

Queue Queue__new(QueueIdentityPtr identity, QueuePagePtr outgoing, QueuePagePtr incoming, QueueConnectionID connection,
                 QueueParticipantID peer_pid);
Result(Queue) Queue__attach(QueueIdentityPtr identity, QueuePagePtr client, QueuePagePtr server,
                            QueueConnectionID connection, QueueParticipantID pid);
QueueCode Queue__status(QueuePtr self);
// Observes shared cursors without consuming messages or changing endpoint state.
bool Queue__has_unread_messages(QueuePtr self);
QueueConnectionID Queue__connection(QueuePtr self);
QueueParticipantID Queue__peer_pid(QueuePtr self);
QueueParticipantID Queue__local_pid(QueuePtr self);
QueueParticipantID Queue__origin_pid(QueuePtr self);
QueueProgress Queue__progress(QueuePtr self);
bool QueueProgress__equals(ConstQueueProgressPtr self, ConstQueueProgressPtr other);
Result(QueueCode) Queue__push(QueuePtr self, ConstQueueMessagePtr message);
Result(QueueMessage) Queue__pop(QueuePtr self);

#endif
