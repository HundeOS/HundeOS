#ifndef USER_SYS_QUEUE_H
#define USER_SYS_QUEUE_H

#include "ipc/queue.h"

ResultDef(QueueArrivalsPtr);

Result(QueueArrivalsPtr) Queue__listen(ConstBStringPtr name);
Result(Queue) Queue__connect(QueueEndpointID endpoint);
Result(Queue) Queue__accept(QueueConnectionID connection);
Result(Queue) Queue__attach_existing(QueueConnectionID connection);
Result(QueueConnectionID) Queue__delegate(QueueConnectionID client_connection, QueueEndpointID endpoint);
QueueCode Queue__cancel_delegation(QueueConnectionID client_connection, QueueConnectionID delegated);
QueueCode QueueConnection__close(QueueConnectionID connection);

// Event waits publish the observed process doorbell generation before parking.
// The kernel checks live queue and notification state before committing the wait.
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
