#include "shared_wait.h"
#include "../kernel_core.h"
#include "ipc/queue_wait_control.h"

Result(Address) Process__queue_wait_control(ProcessPtr self)
{
    match_optional(
        self->queue_wait_control,
        some(mapping)
        {
            return Success(Address, mapping.virtual_addr);
        },
        none_do_nothing()
    );
    match_result(
        Process__alloc_user_pages(self, sizeof(QueueWaitControl)),
        success(mapping)
        {
            Kernel__memset(mapping.kernel_addr.virtual_addr, 0, sizeof(QueueWaitControl));
            self->queue_wait_control = Some(UserAddress, mapping);
            return Success(Address, mapping.virtual_addr);
        },
        error(code, message)
        {
            return Error(Address, message, code);
        }
    );
    return Error(Address, bstr("Wait control allocation failed"), SERVICE_MAP_FAILED);
}

void Process__wake_shared_wait(ProcessPtr self)
{
    match_optional(
        self->queue_wait_control,
        some(mapping)
        {
            QueueWaitControl__wake(k_v_ptr(mapping, QueueWaitControl));
        },
        none_do_nothing()
    );
}

static bool Process__has_pending_notifications(ProcessPtr self)
{
    SchedulerPtr scheduler = self->kernel->scheduler;
    bool events_pending = false;
    critical_section(
        scheduler->service_event_dispatcher.lock,
        {
            events_pending = ServiceEvents__len(&self->service.events) != 0;
        }
    );
    return events_pending || Buffers__has_pending_notifications(&scheduler->buffers, self) ||
           IRQDispatcher__has_pending_notifications(self->kernel->irq_dispatcher, self);
}

bool Process__has_pending_messages(ProcessPtr self)
{
    return Process__has_pending_notifications(self) ||
           Queues__has_unread_messages(&self->kernel->scheduler->queues, self);
}

static bool Process__shared_observations_ready(ProcessPtr self, ConstQueueWaitControlPtr control,
                                               ConstQueueWaitPublicationPtr publication)
{
    uint64_t now = CPU__time_usec();
    if (publication->deadline != QUEUE_WAIT_NO_DEADLINE &&
        (publication->deadline <= now || publication->deadline - now > QUEUE_WAIT_MAX_RECHECK_USEC))
    {
        return true;
    }

    if (QueueWaitControl__woken(control, publication->observed_wake_generation))
    {
        return true;
    }
    SchedulerPtr scheduler = self->kernel->scheduler;
    for (uint64_t index = 0; index < publication->count; index++)
    {
        match_result(
            QueueWaitObservations__at(&control->observations, index),
            success(observed)
            {
                if (Queues__observation_changed(&scheduler->queues, self, &observed))
                {
                    return true;
                }
            },
            error(code, message)
            {
                return true;
            }
        );
    }
    return !QueueWaitControl__unchanged(control, publication);
}

Optional(QueueWaitPublication) Process__shared_wait(ProcessPtr self)
{
    if (Process__has_pending_signal(self) || Process__has_pending_notifications(self))
    {
        return None(QueueWaitPublication);
    }

    match_optional(
        self->queue_wait_control,
        some(mapping)
        {
            ConstQueueWaitControlPtr control = k_v_ptr(mapping, QueueWaitControl);
            match_optional(
                QueueWaitControl__publication(control),
                some(publication)
                {
                    if (!Process__shared_observations_ready(self, control, &publication))
                    {
                        return Some(QueueWaitPublication, publication);
                    }
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return None(QueueWaitPublication);
}

bool Process__shared_wait_ready(ProcessPtr self)
{
    match_optional(Process__shared_wait(self),
        some(publication)
        {
            return false;
        },
        none_do_nothing()
    );
    return true;
}

QueueWaitCompletedUsec Process__user_wait_usec(ProcessPtr self, QueueWaitStartedAt sampled_at_usec)
{
    match_optional(
        self->queue_wait_control,
        some(mapping)
        {
            ConstQueueWaitControlPtr control = k_v_ptr(mapping, QueueWaitControl);
            match_optional(
                QueueWaitControl__publication(control),
                some(publication)
                {
                    QueueWaitStartedAt started_at_usec = publication.started_at_usec;
                    if (started_at_usec > sampled_at_usec)
                    {
                        started_at_usec = sampled_at_usec;
                    }
                    return publication.completed_wait_usec + sampled_at_usec - started_at_usec;
                },
                none()
                {
                    return QueueWaitControl__completed_wait_usec(control);
                }
            );
        },
        none_do_nothing()
    );
    return 0;
}
