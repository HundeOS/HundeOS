#ifndef KERNEL_QUEUE_WAIT_USAGE_H
#define KERNEL_QUEUE_WAIT_USAGE_H

#include "../mutex.h"
#include "../../common/process/process_registry.h"
#include "../../common/ipc/queue_wait.h"

typedef enum QueueWaitUsageState
{
    QUEUE_WAIT_USAGE_IDLE,
    QUEUE_WAIT_USAGE_PARKED,
    QUEUE_WAIT_USAGE_RUNNABLE
} QueueWaitUsageState;

typedef struct QueueWaitUsage
{
    mutex_t lock;
    QueueWaitUsageState state;
    uint64_t since_usec;
    ProcessQueueWaitUsage totals;
} QueueWaitUsage;

typedef QueueWaitUsage * QueueWaitUsagePtr;

QueueWaitUsage QueueWaitUsage__new(void);
void QueueWaitUsage__park(QueueWaitUsagePtr self);
void QueueWaitUsage__wake(QueueWaitUsagePtr self, QueueWaitCode reason);
void QueueWaitUsage__cancel(QueueWaitUsagePtr self);
void QueueWaitUsage__select(QueueWaitUsagePtr self);
void QueueWaitUsage__select_speculative(QueueWaitUsagePtr self);
ProcessQueueWaitUsage QueueWaitUsage__snapshot(QueueWaitUsagePtr self);

#endif
