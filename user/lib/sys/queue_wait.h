#ifndef USER_QUEUE_WAIT_H
#define USER_QUEUE_WAIT_H

#include "queue.h"
#include "ipc/queue_wait.h"
#include "ipc/queue_wait_control.h"
#include "syscalls/types.h"

typedef Result(QueueWaitCode) (*QueueWaitPark)(Anything context);
typedef void (*QueueWaitNotify)(Anything context);

typedef struct QueueWaitAdapter
{
    Anything context;
    QueueWaitPark park;
    QueueWaitNotify notify;
} QueueWaitAdapter;

OptionalDef(QueueWaitAdapter);

static inline QueueWaitAdapter QueueWaitAdapter__new(Anything context, QueueWaitPark park, QueueWaitNotify notify)
{
    return (QueueWaitAdapter){.context = context, .park = park, .notify = notify};
}

typedef struct QueueWaitGate
{
    bool occupied;
} QueueWaitGate;

typedef QueueWaitGate * QueueWaitGatePtr;

#define QUEUE_WAIT_GATE_INITIALIZER {.occupied = false}

Optional(QueueWaitAdapter) QueueWait__swap_adapter(const OptionalPtr(QueueWaitAdapter) adapter);
bool QueueWait__has_adapter(void);
Result(QueueWaitCode) QueueWait__cooperate(void);
void QueueWait__notify(void);
Result(QueueWaitCode) QueueWaitGate__acquire(QueueWaitGatePtr self);
void QueueWaitGate__release(QueueWaitGatePtr self);

Optional(QueueDispatchTrace) QueueWait__take_dispatch_trace(ConstRequestTicketPtr ticket);
void QueueWait__reset_after_fork(void);

#endif
