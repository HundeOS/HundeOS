#include "kernel_notification_queue.h"

KernelNotificationQueue KernelNotificationQueue__new(void)
{
    return (KernelNotificationQueue){
        .pending = KernelNotificationSlots__new(),
        .occurred_at_usec = KernelNotificationTimestamps__new(),
    };
}

bool KernelNotificationQueue__has_pending(KernelNotificationQueuePtr self)
{
    for_each_atomic_fa_ptr(
        KernelNotificationSlots,
        &self->pending,
        id,
        sequence,
        {
            if (sequence != 0)
            {
                return true;
            }
        }
    );

    return false;
}

Result(CollectionCode) KernelNotificationQueue__publish(KernelNotificationQueuePtr self, KernelNotificationID id,
                                                        KernelNotificationSequence sequence, uint64_t occurred_at_usec)
{
    if (sequence == 0)
    {
        return Error(CollectionCode, FIXED_ARRAY_ERROR(ARRAY_INDEX_OUT_OF_BOUNDS), ARRAY_OUT_OF_BOUNDS);
    }
    match_result(
        KernelNotificationTimestamps__set(&self->occurred_at_usec, id, occurred_at_usec),
        success_do_nothing(),
        error(code, message)
        {
            return Error(CollectionCode, message, code);
        }
    );
    return KernelNotificationSlots__set(&self->pending, id, sequence);
}

Optional(KernelNotification) KernelNotificationQueue__pop(KernelNotificationQueuePtr self)
{
    for (KernelNotificationID id = 0; id < KernelNotificationSlots__capacity(&self->pending); id++)
    {
        match_optional(
            KernelNotificationSlots__take(&self->pending, id),
            some(sequence)
            {
                if (sequence != 0)
                {
                    uint64_t occurred_at_usec =
                        unwrap_optional(KernelNotificationTimestamps__take(&self->occurred_at_usec, id), 0);
                    return Some(KernelNotification, KernelNotification__new(id, sequence, occurred_at_usec));
                }
            },
            none_do_nothing()
        );
    }
    return None(KernelNotification);
}
