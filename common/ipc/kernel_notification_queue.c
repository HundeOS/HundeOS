#include "kernel_notification_queue.h"

KernelNotificationQueue KernelNotificationQueue__new(void)
{
    return (KernelNotificationQueue){.pending = KernelNotificationSlots__new()};
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
                                                        KernelNotificationSequence sequence)
{
    if (sequence == 0)
    {
        return Error(CollectionCode, FIXED_ARRAY_ERROR(ARRAY_INDEX_OUT_OF_BOUNDS), ARRAY_OUT_OF_BOUNDS);
    }
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
                    return Some(KernelNotification, KernelNotification__new(id, sequence));
                }
            },
            none_do_nothing()
        );
    }
    return None(KernelNotification);
}
