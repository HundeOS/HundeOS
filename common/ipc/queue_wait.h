#ifndef HUND_QUEUE_WAIT_H
#define HUND_QUEUE_WAIT_H

#include "queue.h"

#define QUEUE_WAIT_INDEFINITE_TICKS 0ULL
#define QUEUE_WAIT_DEFAULT_TICKS 10ULL
#define QUEUE_WAIT_MAX_TICKS 100ULL

typedef enum QueueWaitCode
{
    QUEUE_WAIT_CHANGED = 0,
    QUEUE_WAIT_TIMEOUT = 1,
    QUEUE_WAIT_PARKED = 2,
} QueueWaitCode;

ResultDef(QueueWaitCode);

#endif
