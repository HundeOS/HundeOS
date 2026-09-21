#ifndef USER_QUEUE_WAIT_H
#define USER_QUEUE_WAIT_H

#include "queue.h"
#include "ipc/queue_wait.h"

Result(QueueCode) QueueWait__remember_queue(QueuePtr queue);
Result(QueueCode) QueueWait__remember_arrivals(QueueArrivalsPtr arrivals);
Result(QueueCode) QueueWait__forget(QueueConnectionID connection);

#endif
