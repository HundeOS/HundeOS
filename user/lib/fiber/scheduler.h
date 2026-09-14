#ifndef __USER_LIB_FIBER_SCHEDULER_H
#define __USER_LIB_FIBER_SCHEDULER_H

#include "fiber.h"
#include "types/fixed_array.h"

#define FIBER_SCHEDULER_MAX_FIBERS 64
#define FIBER_SCHEDULER_DEFAULT_MAX_FIBERS 16

bool FiberPtr__equals(void * self, void * other);

FixedArrayDefine(FiberPtr, FIBER_SCHEDULER_MAX_FIBERS, FiberQueue, FiberPtr__equals);
FixedArrayDefine(FiberPtr, FIBER_SCHEDULER_MAX_FIBERS, FiberWaiters, FiberPtr__equals);

struct FiberScheduler
{
    FiberContext context;
    FiberQueue ready;
    FiberWaiters waiting;
    Optional(FiberPtr) current;
    size_t live_count;
    size_t max_fibers;
};

FiberScheduler FiberScheduler__new(void);
FiberScheduler FiberScheduler__new_with_max_fibers(size_t max_fibers);
Result(FiberPtr) FiberScheduler__spawn(FiberSchedulerPtr self, FiberEntry entry, Anything argument);
Result(FiberCode) FiberScheduler__run_ready(FiberSchedulerPtr self);
Result(FiberCode) FiberScheduler__run_ready_until_blocked(FiberSchedulerPtr self);
Result(FiberCode) FiberScheduler__await_current(FiberSchedulerPtr self, FiberWaitKey key);
Result(FiberCode) FiberScheduler__await_channel_signal(FiberSchedulerPtr self, ChannelSignalPtr channel);
Result(FiberCode) FiberScheduler__wake(FiberSchedulerPtr self, FiberWaitKey key, FiberWakeValue value);
Result(FiberCode) FiberScheduler__wake_channel_signal(FiberSchedulerPtr self, ChannelSignalPtr channel);
Optional(FiberPtr) FiberScheduler__current(FiberSchedulerPtr self);
size_t FiberScheduler__live_count(FiberSchedulerPtr self);
size_t FiberScheduler__max_fibers(FiberSchedulerPtr self);
size_t FiberScheduler__ready_count(FiberSchedulerPtr self);
size_t FiberScheduler__waiting_count(FiberSchedulerPtr self);

#endif
