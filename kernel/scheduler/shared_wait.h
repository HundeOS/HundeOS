#ifndef HUND_KERNEL_SHARED_WAIT_H
#define HUND_KERNEL_SHARED_WAIT_H

#include "../process.h"
#include "../kalloc.h"
#include "ipc/doorbell.h"
#include "ipc/queue_wait_control.h"

Result(UserAddress) DoorbellBacking__new(void);
void DoorbellBacking__retain(UserAddress self);
Result(KAllocCode) DoorbellBacking__release(UserAddress self);

// Caller holds the scheduler lock so queue mappings cannot disappear mid-read.
bool Process__shared_wait_armed(ProcessPtr self);
bool Process__shared_wait_ready(ProcessPtr self);
bool Process__doorbell_changed(ProcessPtr self, IpcEventGeneration observed);
void Process__arm_kernel_queue_wait(ProcessPtr self, IpcEventGeneration observed);
void Process__clear_kernel_queue_wait(ProcessPtr self);
bool Process__kernel_queue_wait_ready(ProcessPtr self);
Optional(QueueWaitPublication) Process__shared_wait(ProcessPtr self);
QueueWaitCompletedUsec Process__user_wait_usec(ProcessPtr self, QueueWaitStartedAt sampled_at_usec);
bool Process__has_pending_messages(ProcessPtr self);
bool Process__has_pending_queue_wait(ProcessPtr self);
void Process__publish_dispatch_trace(ProcessPtr self, ConstRequestTicketPtr ticket, uint64_t eligible_at_usec,
                                     uint64_t selected_at_usec, uint64_t dispatched_at_usec);

#endif
