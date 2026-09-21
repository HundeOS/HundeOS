#ifndef HUND_QUEUE_WAIT_CONTROL_H
#define HUND_QUEUE_WAIT_CONTROL_H

#include "queue_wait.h"
#include "io_diagnostics.h"
#include "../types/atomic_record_array.h"

typedef uint64_t QueueWaitGeneration;
typedef uint64_t QueueWaitDeadline;
typedef uint64_t QueueWaitWakeGeneration;
typedef uint64_t QueueWaitStartedAt;
typedef uint64_t QueueWaitCompletedUsec;
OptionalDef(QueueWaitStartedAt);

#define QUEUE_WAIT_MAX_RECHECK_USEC 100000ULL
#define QUEUE_WAIT_NO_DEADLINE 0ULL

ResultDef(QueueWaitSnapshot);
AtomicRecordArrayDefine(QueueWaitSnapshot, QUEUE_WAIT_LIMIT, QueueWaitObservations);

// Userspace writes the publication. The kernel only increments wake_generation;
// odd publication generations denote an update and must never suppress scheduling.
typedef struct QueueWaitControl
{
    QueueWaitGeneration generation;
    uint64_t count;
    QueueWaitStartedAt started_at_usec;
    QueueWaitCompletedUsec completed_wait_usec;
    QueueWaitDeadline deadline;
    QueueWaitWakeGeneration wake_generation;
    QueueWaitWakeGeneration observed_wake_generation;
    QueueWaitObservations observations;
    IODiagnostics diagnostics;
} QueueWaitControl;
typedef QueueWaitControl * QueueWaitControlPtr;
typedef const QueueWaitControl * ConstQueueWaitControlPtr;
OptionalDef(QueueWaitControlPtr);

typedef struct QueueWaitPublication
{
    QueueWaitGeneration generation;
    uint64_t count;
    QueueWaitStartedAt started_at_usec;
    QueueWaitCompletedUsec completed_wait_usec;
    QueueWaitDeadline deadline;
    QueueWaitWakeGeneration observed_wake_generation;
} QueueWaitPublication;
typedef const QueueWaitPublication * ConstQueueWaitPublicationPtr;
OptionalDef(QueueWaitPublication);

void QueueWaitControl__begin(QueueWaitControlPtr self);
Result(CollectionCode) QueueWaitControl__add(QueueWaitControlPtr self, ConstQueueWaitSnapshotPtr observation);
void QueueWaitControl__commit(QueueWaitControlPtr self, QueueWaitStartedAt started_at_usec, QueueWaitDeadline deadline,
                              QueueWaitWakeGeneration observed_wake_generation);
void QueueWaitControl__clear(QueueWaitControlPtr self);
void QueueWaitControl__complete_wait(QueueWaitControlPtr self, QueueWaitStartedAt completed_at_usec);
QueueWaitCompletedUsec QueueWaitControl__completed_wait_usec(ConstQueueWaitControlPtr self);
void QueueWaitControl__wake(QueueWaitControlPtr self);
QueueWaitWakeGeneration QueueWaitControl__wake_generation(ConstQueueWaitControlPtr self);
bool QueueWaitControl__woken(ConstQueueWaitControlPtr self, QueueWaitWakeGeneration observed_wake_generation);
Optional(QueueWaitPublication) QueueWaitControl__publication(ConstQueueWaitControlPtr self);
bool QueueWaitControl__unchanged(ConstQueueWaitControlPtr self, ConstQueueWaitPublicationPtr publication);

#endif
