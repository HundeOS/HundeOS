#include "../../sys/queue.h"

void Queue__prepare_wait(void)
{
    __asm__ volatile("sevl" ::: "memory");
}

void Queue__wait_event(void)
{
    __asm__ volatile("wfe" ::: "memory");
}

void Queue__wait_readable(QueuePtr self)
{
    __asm__ volatile("sevl" ::: "memory");
    while (Queue__status(self) != QUEUE_CLOSED &&
           (Queue__status(self) == QUEUE_PENDING ||
            __atomic_load_n(&self->incoming->produced, __ATOMIC_ACQUIRE) == self->read))
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}

void Queue__wait_writable(QueuePtr self)
{
    __asm__ volatile("sevl" ::: "memory");
    while (Queue__status(self) != QUEUE_CLOSED &&
           (Queue__status(self) == QUEUE_PENDING ||
            self->written - __atomic_load_n(&self->incoming->consumed, __ATOMIC_ACQUIRE) == QUEUE_CAPACITY))
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}

void QueueArrivals__wait(QueueArrivalsPtr self, QueueEpoch observed_epoch)
{
    __asm__ volatile("sevl" ::: "memory");
    while (QueueArrivals__epoch(self) == observed_epoch)
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}
