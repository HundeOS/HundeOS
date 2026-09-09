#include "atomic_message_queue_layout.h"

Address AtomicMessageQueueLayout__address(AtomicMessageQueueConnectionID connection)
{
    return ATOMIC_MESSAGE_QUEUE_CONNECTION_BASE +
           (connection % ATOMIC_MESSAGE_QUEUE_LIMIT) * ATOMIC_MESSAGE_QUEUE_CONNECTION_SIZE;
}

Optional(AtomicMessageQueueConnectionID)
    AtomicMessageQueueArrivals__connection(AtomicMessageQueueArrivalsPtr self, AtomicMessageQueueSlotIndex index)
{
    match_optional(
        AtomicMessageQueueConnectionSlots__at(&self->connections, index),
        some(connection) { return Some(AtomicMessageQueueConnectionID, connection); },
        none() { return None(AtomicMessageQueueConnectionID); }
    );
}

AtomicMessageQueueEpoch AtomicMessageQueueArrivals__epoch(AtomicMessageQueueArrivalsPtr self)
{
    return __atomic_load_n(&self->epoch, __ATOMIC_ACQUIRE);
}
