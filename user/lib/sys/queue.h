#ifndef USER_SYS_QUEUE_H
#define USER_SYS_QUEUE_H

#include "ipc/queue.h"

ResultDef(QueueArrivalsPtr);

Result(QueueArrivalsPtr) Queue__listen(ConstBStringPtr name);
Result(Queue) Queue__connect(QueueEndpointID endpoint);
Result(Queue) Queue__accept(QueueConnectionID connection);
QueueCode QueueConnection__close(QueueConnectionID connection);

// These waits recheck all predicates after every architectural event wakeup.
void Queue__prepare_wait(void);
void Queue__wait_event(void);
void Queue__wait_readable(QueuePtr self);
void Queue__wait_writable(QueuePtr self);
void QueueArrivals__wait(QueueArrivalsPtr self, QueueEpoch observed_epoch);

#endif
