#include "queue_layout.h"

Address QueueLayout__address(QueueConnectionID connection)
{
    return QUEUE_CONNECTION_BASE + (connection % QUEUE_LIMIT) * QUEUE_CONNECTION_SIZE;
}

Address QueueLayout__peer_doorbell_address(QueueConnectionID connection)
{
    return QUEUE_PEER_DOORBELL_BASE + (connection % QUEUE_LIMIT) * QUEUE_PAGE_SIZE;
}

Optional(QueueConnectionID) QueueArrivals__connection(QueueArrivalsPtr self, QueueSlotIndex index)
{
    match_optional(
        QueueConnectionSlots__at(&self->connections, index),
        some(connection)
        {
            return Some(QueueConnectionID, connection);
        },
        none()
        {
            return None(QueueConnectionID);
        }
    );
}

QueueEpoch QueueArrivals__epoch(QueueArrivalsPtr self)
{
    return __atomic_load_n(&self->epoch, __ATOMIC_ACQUIRE);
}
