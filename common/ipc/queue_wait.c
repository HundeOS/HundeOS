#include "queue_wait.h"

QueueWaitSnapshot QueueWaitSnapshot__new_connection(QueuePtr queue)
{
    return (QueueWaitSnapshot){
        .kind = QUEUE_WAIT_CONNECTION,
        .handle = queue->connection,
        .produced = __atomic_load_n(&queue->incoming->produced, __ATOMIC_ACQUIRE),
        .consumed = __atomic_load_n(&queue->incoming->consumed, __ATOMIC_ACQUIRE),
        .accepted = __atomic_load_n(&queue->identity->accepted, __ATOMIC_ACQUIRE),
        .closed = __atomic_load_n(&queue->identity->closed, __ATOMIC_ACQUIRE),
    };
}

QueueWaitSnapshot QueueWaitSnapshot__new_arrivals(QueueArrivalsPtr arrivals)
{
    return (QueueWaitSnapshot){
        .kind = QUEUE_WAIT_ARRIVALS,
        .handle = arrivals->endpoint,
        .produced = QueueArrivals__epoch(arrivals),
    };
}

bool QueueWaitSnapshot__equals(ConstQueueWaitSnapshotPtr self, ConstQueueWaitSnapshotPtr other)
{
    return self->kind == other->kind && self->handle == other->handle && self->produced == other->produced &&
           self->consumed == other->consumed && self->accepted == other->accepted && self->closed == other->closed;
}
