#include "../../sys/atomic_message_queue.h"

void atomic_message_queue_wait_readable(AtomicMessageQueuePtr queue)
{
    __asm__ volatile("sevl" ::: "memory");
    while (AtomicMessageQueue__status(queue) != ATOMIC_MESSAGE_QUEUE_CLOSED &&
           (AtomicMessageQueue__status(queue) == ATOMIC_MESSAGE_QUEUE_PENDING ||
            __atomic_load_n(&queue->incoming->produced, __ATOMIC_ACQUIRE) == queue->read))
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}

void atomic_message_queue_wait_writable(AtomicMessageQueuePtr queue)
{
    __asm__ volatile("sevl" ::: "memory");
    while (AtomicMessageQueue__status(queue) != ATOMIC_MESSAGE_QUEUE_CLOSED &&
           (AtomicMessageQueue__status(queue) == ATOMIC_MESSAGE_QUEUE_PENDING ||
            queue->written - __atomic_load_n(&queue->incoming->consumed, __ATOMIC_ACQUIRE) ==
                ATOMIC_MESSAGE_QUEUE_CAPACITY))
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}

void atomic_message_queue_wait_arrivals(AtomicMessageQueueArrivalsPtr arrivals, AtomicMessageQueueEpoch observed_epoch)
{
    __asm__ volatile("sevl" ::: "memory");
    while (AtomicMessageQueueArrivals__epoch(arrivals) == observed_epoch)
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}
