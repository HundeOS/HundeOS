#include "queue_wait_usage.h"
#include "../arch/cpu.h"

QueueWaitUsage QueueWaitUsage__new(void)
{
    return (QueueWaitUsage){.lock = MUTEX_INIT, .state = QUEUE_WAIT_USAGE_IDLE};
}

void QueueWaitUsage__park(QueueWaitUsagePtr self)
{
    critical_section(
        self->lock,
        {
            self->state = QUEUE_WAIT_USAGE_PARKED;
            self->since_usec = CPU__time_usec();
            self->totals.parks++;
        }
    );
}

void QueueWaitUsage__wake(QueueWaitUsagePtr self, QueueWaitCode reason)
{
    critical_section(
        self->lock,
        {
            if (self->state != QUEUE_WAIT_USAGE_PARKED ||
                (reason != QUEUE_WAIT_CHANGED && reason != QUEUE_WAIT_TIMEOUT))
            {
                cs_return;
            }
            uint64_t now = CPU__time_usec();
            uint64_t elapsed = now - self->since_usec;
            self->totals.parked_usec += elapsed;
            if (elapsed > self->totals.max_parked_usec)
            {
                self->totals.max_parked_usec = elapsed;
            }
            if (reason == QUEUE_WAIT_CHANGED)
            {
                self->totals.activity_wakes++;
            }
            else
            {
                self->totals.timeout_wakes++;
            }
            self->since_usec = now;
            self->state = QUEUE_WAIT_USAGE_RUNNABLE;
        }
    );
}

void QueueWaitUsage__cancel(QueueWaitUsagePtr self)
{
    critical_section(
        self->lock,
        {
            if (self->state != QUEUE_WAIT_USAGE_PARKED)
            {
                cs_return;
            }
            uint64_t elapsed = CPU__time_usec() - self->since_usec;
            self->totals.parked_usec += elapsed;
            if (elapsed > self->totals.max_parked_usec)
            {
                self->totals.max_parked_usec = elapsed;
            }
            self->state = QUEUE_WAIT_USAGE_IDLE;
        }
    );
}

void QueueWaitUsage__select(QueueWaitUsagePtr self)
{
    critical_section(
        self->lock,
        {
            if (self->state != QUEUE_WAIT_USAGE_RUNNABLE)
            {
                cs_return;
            }
            uint64_t elapsed = CPU__time_usec() - self->since_usec;
            self->totals.runnable_usec += elapsed;
            if (elapsed > self->totals.max_runnable_usec)
            {
                self->totals.max_runnable_usec = elapsed;
            }
            self->totals.selections++;
            self->state = QUEUE_WAIT_USAGE_IDLE;
        }
    );
}

void QueueWaitUsage__select_speculative(QueueWaitUsagePtr self)
{
    critical_section(
        self->lock,
        {
            self->totals.speculative_selections++;
        }
    );
}

ProcessQueueWaitUsage QueueWaitUsage__snapshot(QueueWaitUsagePtr self)
{
    critical_section(
        self->lock,
        {
            cs_return self->totals;
        }
    );
}
