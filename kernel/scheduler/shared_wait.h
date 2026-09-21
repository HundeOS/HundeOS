#ifndef HUND_KERNEL_SHARED_WAIT_H
#define HUND_KERNEL_SHARED_WAIT_H

#include "../process.h"
#include "ipc/queue_wait_control.h"

// Caller holds the scheduler lock so queue mappings cannot disappear mid-read.
bool Process__shared_wait_ready(ProcessPtr self);
Optional(QueueWaitPublication) Process__shared_wait(ProcessPtr self);
QueueWaitCompletedUsec Process__user_wait_usec(ProcessPtr self, QueueWaitStartedAt sampled_at_usec);
bool Process__has_pending_messages(ProcessPtr self);

#endif
