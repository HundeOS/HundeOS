#include "queue_wait_control.h"

QueueDispatchTrace QueueDispatchTrace__new(ConstRequestTicketPtr ticket, uint64_t eligible_at_usec,
                                           uint64_t selected_at_usec, uint64_t dispatched_at_usec)
{
    return (QueueDispatchTrace){
        .ticket = *ticket,
        .eligible_at_usec = eligible_at_usec,
        .selected_at_usec = selected_at_usec,
        .dispatched_at_usec = dispatched_at_usec,
    };
}

void QueueWaitControl__begin(QueueWaitControlPtr self)
{
    uint64_t generation = __atomic_load_n(&self->generation, __ATOMIC_RELAXED);
    __atomic_store_n(&self->generation, (generation + 1) | 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&self->count, 0, __ATOMIC_RELEASE);
}

Result(CollectionCode) QueueWaitControl__add(QueueWaitControlPtr self, ConstQueueWaitSnapshotPtr observation)
{
    uint64_t count = __atomic_load_n(&self->count, __ATOMIC_RELAXED);
    match_result(
        QueueWaitObservations__set(&self->observations, count, observation),
        success(code)
        {
            __atomic_store_n(&self->count, count + 1, __ATOMIC_RELEASE);
            return Success(CollectionCode, code);
        },
        error(code, message)
        {
            return Error(CollectionCode, message, code);
        }
    );
    return Error(CollectionCode, bstr("Invalid wait observation"), ATOMIC_RECORD_INVALID);
}

void QueueWaitControl__commit(QueueWaitControlPtr self, QueueWaitStartedAt started_at_usec, QueueWaitDeadline deadline,
                              QueueWaitWakeGeneration observed_wake_generation)
{
    __atomic_store_n(&self->started_at_usec, started_at_usec, __ATOMIC_RELEASE);
    __atomic_store_n(&self->deadline, deadline, __ATOMIC_RELEASE);
    __atomic_store_n(&self->observed_wake_generation, observed_wake_generation, __ATOMIC_RELEASE);
    uint64_t generation = __atomic_load_n(&self->generation, __ATOMIC_RELAXED);
    __atomic_store_n(&self->generation, (generation + 1) & ~1ULL, __ATOMIC_RELEASE);
}

void QueueWaitControl__clear(QueueWaitControlPtr self)
{
    QueueWaitControl__begin(self);
}

void QueueWaitControl__wake(QueueWaitControlPtr self)
{
    __atomic_fetch_add(&self->wake_generation, 1, __ATOMIC_RELEASE);
}

QueueWaitWakeGeneration QueueWaitControl__wake_generation(ConstQueueWaitControlPtr self)
{
    return __atomic_load_n(&self->wake_generation, __ATOMIC_ACQUIRE);
}

bool QueueWaitControl__woken(ConstQueueWaitControlPtr self, QueueWaitWakeGeneration observed_wake_generation)
{
    return QueueWaitControl__wake_generation(self) != observed_wake_generation;
}

static QueueWaitPublication QueueWaitPublication__new(ConstQueueWaitControlPtr control)
{
    QueueWaitGeneration generation = __atomic_load_n(&control->generation, __ATOMIC_ACQUIRE);
    uint64_t count = __atomic_load_n(&control->count, __ATOMIC_ACQUIRE);
    QueueWaitStartedAt started_at_usec = __atomic_load_n(&control->started_at_usec, __ATOMIC_ACQUIRE);
    QueueWaitCompletedUsec completed_wait_usec = __atomic_load_n(&control->completed_wait_usec, __ATOMIC_ACQUIRE);
    QueueWaitDeadline deadline = __atomic_load_n(&control->deadline, __ATOMIC_ACQUIRE);
    QueueWaitWakeGeneration observed_wake_generation =
        __atomic_load_n(&control->observed_wake_generation, __ATOMIC_ACQUIRE);
    return (QueueWaitPublication){
        .generation = generation,
        .count = count,
        .started_at_usec = started_at_usec,
        .completed_wait_usec = completed_wait_usec,
        .deadline = deadline,
        .observed_wake_generation = observed_wake_generation,
    };
}

Optional(QueueWaitPublication) QueueWaitControl__publication(ConstQueueWaitControlPtr self)
{
    QueueWaitPublication publication = QueueWaitPublication__new(self);
    if ((publication.generation & 1) != 0 || publication.count == 0 || publication.count > QUEUE_WAIT_LIMIT ||
        !QueueWaitControl__unchanged(self, &publication))
    {
        return None(QueueWaitPublication);
    }
    return Some(QueueWaitPublication, publication);
}

void QueueWaitControl__complete_wait(QueueWaitControlPtr self, QueueWaitStartedAt completed_at_usec)
{
    Optional(QueueWaitPublication) current = QueueWaitControl__publication(self);
    QueueWaitControl__clear(self);
    match_optional(
        current,
        some(publication)
        {
            if (completed_at_usec > publication.started_at_usec)
            {
                __atomic_fetch_add(&self->completed_wait_usec, completed_at_usec - publication.started_at_usec,
                                   __ATOMIC_RELEASE);
            }
        },
        none_do_nothing()
    );
}

QueueWaitCompletedUsec QueueWaitControl__completed_wait_usec(ConstQueueWaitControlPtr self)
{
    return __atomic_load_n(&self->completed_wait_usec, __ATOMIC_ACQUIRE);
}

bool QueueWaitControl__unchanged(ConstQueueWaitControlPtr self, ConstQueueWaitPublicationPtr publication)
{
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    return __atomic_load_n(&self->generation, __ATOMIC_ACQUIRE) == publication->generation;
}

void QueueWaitControl__publish_dispatch(QueueWaitControlPtr self, ConstQueueDispatchTracePtr trace)
{
    uint64_t generation = __atomic_load_n(&self->dispatch_trace.generation, __ATOMIC_RELAXED);
    uint64_t updating = (generation + 1) | 1ULL;
    __atomic_store_n(&self->dispatch_trace.generation, updating, __ATOMIC_SEQ_CST);
    self->dispatch_trace.trace = *trace;
    __atomic_store_n(&self->dispatch_trace.generation, updating + 1, __ATOMIC_RELEASE);
}

Optional(QueueDispatchTrace) QueueWaitControl__take_dispatch(QueueWaitControlPtr self, ConstRequestTicketPtr ticket)
{
    uint64_t before = __atomic_load_n(&self->dispatch_trace.generation, __ATOMIC_ACQUIRE);
    uint64_t consumed = __atomic_load_n(&self->dispatch_trace.consumed_generation, __ATOMIC_ACQUIRE);
    if (before == 0 || (before & 1) != 0 || before == consumed)
    {
        return None(QueueDispatchTrace);
    }
    QueueDispatchTrace trace = self->dispatch_trace.trace;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&self->dispatch_trace.generation, __ATOMIC_ACQUIRE) != before ||
        !RequestTicket__equals(&trace.ticket, ticket))
    {
        return None(QueueDispatchTrace);
    }
    __atomic_store_n(&self->dispatch_trace.consumed_generation, before, __ATOMIC_RELEASE);
    return Some(QueueDispatchTrace, trace);
}
