#ifndef HUND_QUEUE_WAIT_H
#define HUND_QUEUE_WAIT_H

#include "queue.h"

#define QUEUE_WAIT_LIMIT (QUEUE_LIMIT + QUEUE_LISTENER_LIMIT)
#define QUEUE_WAIT_INDEFINITE_TICKS 0ULL
#define QUEUE_WAIT_DEFAULT_TICKS 10ULL
#define QUEUE_WAIT_MAX_TICKS 100ULL

typedef enum QueueWaitKind
{
    QUEUE_WAIT_CONNECTION,
    QUEUE_WAIT_ARRIVALS,
} QueueWaitKind;

typedef enum QueueWaitCode
{
    QUEUE_WAIT_CHANGED = 0,
    QUEUE_WAIT_TIMEOUT = 1,
    QUEUE_WAIT_PARKED = 2,
} QueueWaitCode;

ResultDef(QueueWaitCode);

typedef struct QueueWaitSnapshot
{
    QueueWaitKind kind;
    QueueHandleID handle;
    QueueCursor produced;
    QueueCursor consumed;
    QueueStateFlag accepted;
    QueueStateFlag closed;
} QueueWaitSnapshot;

typedef QueueWaitSnapshot * QueueWaitSnapshotPtr;
typedef const QueueWaitSnapshot * ConstQueueWaitSnapshotPtr;
OptionalDef(QueueWaitSnapshot);

QueueWaitSnapshot QueueWaitSnapshot__new_connection(QueuePtr queue);
QueueWaitSnapshot QueueWaitSnapshot__new_arrivals(QueueArrivalsPtr arrivals);
bool QueueWaitSnapshot__equals(ConstQueueWaitSnapshotPtr self, ConstQueueWaitSnapshotPtr other);

#endif
