#ifndef HUND_QUEUE_WAIT_CONTROL_H
#define HUND_QUEUE_WAIT_CONTROL_H

#include "queue_wait.h"
#include "io_diagnostics.h"
#include "request_ticket.h"
#include "doorbell.h"

typedef uint64_t QueueWaitGeneration;
typedef uint64_t QueueWaitDeadline;
typedef uint64_t QueueWaitStartedAt;
typedef uint64_t QueueWaitCompletedUsec;
OptionalDef(QueueWaitStartedAt);

#define QUEUE_WAIT_MAX_RECHECK_USEC 100000ULL
#define QUEUE_WAIT_NO_DEADLINE 0ULL

typedef struct QueueDispatchTrace
{
    RequestTicket ticket;
    uint64_t eligible_at_usec;
    uint64_t selected_at_usec;
    uint64_t dispatched_at_usec;
} QueueDispatchTrace;

typedef const QueueDispatchTrace * ConstQueueDispatchTracePtr;
OptionalDef(QueueDispatchTrace);

QueueDispatchTrace QueueDispatchTrace__new(ConstRequestTicketPtr ticket, uint64_t eligible_at_usec,
                                           uint64_t selected_at_usec, uint64_t dispatched_at_usec);

typedef struct QueueDispatchTraceSlot
{
    uint64_t generation;
    uint64_t consumed_generation;
    QueueDispatchTrace trace;
} QueueDispatchTraceSlot;

// Userspace publishes the process doorbell generation observed before polling.
// The kernel publishes the lossy dispatch trace. Odd publication generations
// denote an update and must never suppress scheduling.
typedef struct QueueWaitControl
{
    QueueWaitGeneration generation;
    QueueWaitStartedAt started_at_usec;
    QueueWaitCompletedUsec completed_wait_usec;
    QueueWaitDeadline deadline;
    IpcEventGeneration observed_doorbell_generation;
    IODiagnostics diagnostics;
    QueueDispatchTraceSlot dispatch_trace;
} QueueWaitControl;

typedef QueueWaitControl * QueueWaitControlPtr;
typedef const QueueWaitControl * ConstQueueWaitControlPtr;
OptionalDef(QueueWaitControlPtr);

typedef struct QueueWaitPublication
{
    QueueWaitGeneration generation;
    QueueWaitStartedAt started_at_usec;
    QueueWaitCompletedUsec completed_wait_usec;
    QueueWaitDeadline deadline;
    IpcEventGeneration observed_doorbell_generation;
} QueueWaitPublication;

typedef const QueueWaitPublication * ConstQueueWaitPublicationPtr;
OptionalDef(QueueWaitPublication);

void QueueWaitControl__begin(QueueWaitControlPtr self);
void QueueWaitControl__commit(QueueWaitControlPtr self, QueueWaitStartedAt started_at_usec, QueueWaitDeadline deadline,
                              IpcEventGeneration observed_doorbell_generation);
void QueueWaitControl__clear(QueueWaitControlPtr self);
void QueueWaitControl__complete_wait(QueueWaitControlPtr self, QueueWaitStartedAt completed_at_usec);
QueueWaitCompletedUsec QueueWaitControl__completed_wait_usec(ConstQueueWaitControlPtr self);
Optional(QueueWaitPublication) QueueWaitControl__publication(ConstQueueWaitControlPtr self);
bool QueueWaitControl__unchanged(ConstQueueWaitControlPtr self, ConstQueueWaitPublicationPtr publication);
void QueueWaitControl__publish_dispatch(QueueWaitControlPtr self, ConstQueueDispatchTracePtr trace);
Optional(QueueDispatchTrace) QueueWaitControl__take_dispatch(QueueWaitControlPtr self, ConstRequestTicketPtr ticket);

#endif
