#ifndef USER_SYS_QUEUE_H
#define USER_SYS_QUEUE_H

#include "ipc/queue.h"

ResultDef(QueueArrivalsPtr);

Result(QueueArrivalsPtr) Queue__listen(ConstBStringPtr name);
Result(Queue) Queue__connect(QueueEndpointID endpoint);
Result(Queue) Queue__accept(QueueConnectionID connection);
QueueCode QueueConnection__close(QueueConnectionID connection);

// Event waits publish shared scheduler observations then use WFE. Registration
// allocates once; individual waits never invoke a parking syscall.
// Events are hints: unrelated or spurious wakes remain inside the wait.
// Readable/writable waits retain their synchronous contract and return only
// when authoritative state is ready, closed, corrupt, or kernel-notified.
void Queue__prepare_wait(void);
void Queue__wait_event(void);
void Queue__wait_readable(QueuePtr self);
void Queue__wait_writable(QueuePtr self);
bool Queue__wait_readable_interruptible(QueuePtr self);
bool Queue__wait_writable_interruptible(QueuePtr self);
void QueueArrivals__wait(QueueArrivalsPtr self, QueueEpoch observed_epoch);

#endif
