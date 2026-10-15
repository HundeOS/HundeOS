#ifndef HUND_QUEUE_H
#define HUND_QUEUE_H

#include "queue_layout.h"
#include "doorbell.h"
#include "../types/result.h"

typedef void (*QueueDataNotifier)(QueueConnectionID connection);

// Private per-endpoint cursors must not be reconstructed from peer-writable state.
typedef struct Queue
{
    QueueIdentityPtr identity;
    QueuePagePtr outgoing;
    QueuePagePtr incoming;
    QueueConnectionID connection;
    QueueParticipantID peer_pid;
    IpcDoorbellPtr peer_doorbell;
    QueueDataNotifier notify_data_available;
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
                 QueueParticipantID peer_pid, IpcDoorbellPtr peer_doorbell, QueueDataNotifier notify_data_available);
Result(Queue)
    Queue__attach(QueueIdentityPtr identity, QueuePagePtr client, QueuePagePtr server, QueueConnectionID connection,
                  QueueParticipantID pid, IpcDoorbellPtr peer_doorbell, QueueDataNotifier notify_data_available);
void Queue__ignore_data_available(QueueConnectionID connection);
QueueCode Queue__status(QueuePtr self);
// Observes shared cursors without consuming messages or changing endpoint state.
bool Queue__has_unread_messages(QueuePtr self);
bool Queue__has_write_capacity(QueuePtr self);
QueueConnectionID Queue__connection(QueuePtr self);
QueueParticipantID Queue__peer_pid(QueuePtr self);
QueueParticipantID Queue__local_pid(QueuePtr self);
QueueParticipantID Queue__origin_pid(QueuePtr self);
bool Queue__is_channel(QueuePtr self);
QueueProgress Queue__progress(QueuePtr self);
bool QueueProgress__equals(ConstQueueProgressPtr self, ConstQueueProgressPtr other);
Result(QueueCode) Queue__push(QueuePtr self, ConstQueueMessagePtr message);
Result(QueueMessage) Queue__pop(QueuePtr self);
bool Queue__write_data(QueuePtr self, const uint8_t * data, size_t size, uint64_t published_at_usec);
bool Queue__read_data(QueuePtr self, uint8_t * data, size_t size);
uint64_t Queue__data_published_at(QueuePtr self);
uint64_t Queue__written_data_published_at(QueuePtr self);

#endif
