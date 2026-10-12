#include "shared_wait.h"
#include "../kernel_core.h"
#include "ipc/doorbell.h"
#include "ipc/queue_wait_control.h"

Result(UserAddress) Process__doorbell(ProcessPtr self)
{
    match_optional(self->ipc_doorbell,
        some(mapping)
        {
            return Success(UserAddress, mapping);
        },
        none_do_nothing()
    );
    match_result(
        Kernel__try_alloc_user(IPC_DOORBELL_ADDRESS, IPC_DOORBELL_PAGE_SIZE, true, true, false),
        success(mapping)
        {
            Kernel__memset(mapping.kernel_addr.virtual_addr, 0, IPC_DOORBELL_PAGE_SIZE);
            PageTable__map(&self->page_table, self->kernel, mapping, IPC_DOORBELL_PAGE_SIZE);
            match_result(
                Process__record_user_allocation(self, mapping, IPC_DOORBELL_PAGE_SIZE, USER_ALLOCATION_DYNAMIC),
                success(_)
                {
                    self->ipc_doorbell = Some(UserAddress, mapping);
                    return Success(UserAddress, mapping);
                },
                error(code, message)
                {
                    PageTable__unmap(&self->page_table, self->kernel, mapping.virtual_addr, IPC_DOORBELL_PAGE_SIZE);
                    Kernel__free(mapping.kernel_addr.virtual_addr);
                    return Error(UserAddress, message, code);
                }
            );
        },
        error(code, message)
        {
            return Error(UserAddress, message, code);
        }
    );
    return Error(UserAddress, bstr("Doorbell allocation failed"), SERVICE_MAP_FAILED);
}

void Process__ring_doorbell(ProcessPtr self)
{
    match_optional(
        self->ipc_doorbell,
        some(mapping)
        {
            IpcEvent__ring(IpcDoorbell__mutable_event(k_v_ptr(mapping, IpcDoorbell)));
        },
        none_do_nothing()
    );
}

Result(Address) Process__queue_wait_control(ProcessPtr self)
{
    match_result(
        Process__doorbell(self),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Address, message, code);
        }
    );
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
    Process__ring_doorbell(self);
}

void Process__publish_dispatch_trace(ProcessPtr self, ConstRequestTicketPtr ticket, uint64_t eligible_at_usec,
                                     uint64_t selected_at_usec, uint64_t dispatched_at_usec)
{
    match_optional(
        self->queue_wait_control,
        some(mapping)
        {
            QueueDispatchTrace trace =
                QueueDispatchTrace__new(ticket, eligible_at_usec, selected_at_usec, dispatched_at_usec);
            QueueWaitControl__publish_dispatch(k_v_ptr(mapping, QueueWaitControl), &trace);
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

bool Process__has_pending_queue_wait(ProcessPtr self)
{
    return Process__has_pending_notifications(self) ||
           Queues__has_waitable_state(&self->kernel->scheduler->queues, self);
}

bool Process__doorbell_changed(ProcessPtr self, IpcEventGeneration observed)
{
    match_optional(
        self->ipc_doorbell,
        some(mapping)
        {
            return IpcEvent__changed(IpcDoorbell__event(k_v_ptr(mapping, IpcDoorbell)), observed);
        },
        none_do_nothing()
    );
    return true;
}

void Process__arm_kernel_queue_wait(ProcessPtr self, IpcEventGeneration observed)
{
    self->queue_wait_doorbell = Some(IpcEventGeneration, observed);
}

void Process__clear_kernel_queue_wait(ProcessPtr self)
{
    self->queue_wait_doorbell = None(IpcEventGeneration);
}

bool Process__kernel_queue_wait_ready(ProcessPtr self)
{
    if (Process__has_pending_signal(self) || Process__has_pending_queue_wait(self))
    {
        return true;
    }
    match_optional(
        self->queue_wait_doorbell,
        some(observed)
        {
            return Process__doorbell_changed(self, observed);
        },
        none_do_nothing()
    );
    return true;
}

static bool Process__shared_wait_state_ready(ProcessPtr self, ConstQueueWaitControlPtr control,
                                             ConstQueueWaitPublicationPtr publication)
{
    uint64_t now = CPU__time_usec();
    if (publication->deadline != QUEUE_WAIT_NO_DEADLINE &&
        (publication->deadline <= now || publication->deadline - now > QUEUE_WAIT_MAX_RECHECK_USEC))
    {
        return true;
    }

    if (Process__doorbell_changed(self, publication->observed_doorbell_generation) ||
        Process__has_pending_queue_wait(self))
    {
        return true;
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
                    if (!Process__shared_wait_state_ready(self, control, &publication))
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
    if (Process__has_ready_parked_operation(self, PARK_WAIT_QUEUE, &None(OperationKey)))
    {
        return true;
    }
    match_optional(Process__shared_wait(self),
        some(publication)
        {
            return false;
        },
        none_do_nothing()
    );
    return true;
}

bool Process__shared_wait_armed(ProcessPtr self)
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
                    return !Process__doorbell_changed(self, publication.observed_doorbell_generation) &&
                           QueueWaitControl__unchanged(control, &publication);
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return false;
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
