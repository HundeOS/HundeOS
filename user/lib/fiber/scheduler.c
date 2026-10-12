#include "scheduler.h"

static BString queue_full = bstr("Fiber scheduler queue full");
static BString wait_not_found = bstr("Fiber wait key not found");
static BString no_current_fiber = bstr("Fiber scheduler has no current fiber");
static BString fiber_limit_reached = bstr("Fiber scheduler live fiber limit reached");

FixedArrayBodyDefine(FiberQueue);
FixedArrayBodyDefine(FiberWaiters);

bool FiberPtr__equals(void * self, void * other)
{
    return *(FiberPtr *)self == *(FiberPtr *)other;
}

FiberScheduler FiberScheduler__new(void)
{
    return FiberScheduler__new_with_max_fibers(FIBER_SCHEDULER_DEFAULT_MAX_FIBERS);
}

FiberScheduler FiberScheduler__new_with_max_fibers(size_t max_fibers)
{
    return (FiberScheduler){
        .context = {0},
        .ready = FiberQueue__new(),
        .waiting = FiberWaiters__new(),
        .current = None(FiberPtr),
        .live_count = 0,
        .max_fibers = max_fibers,
        .default_stack_size = Fiber__default_stack_size(),
    };
}

Result(FiberPtr) FiberScheduler__spawn(FiberSchedulerPtr self, FiberEntry entry, Anything argument)
{
    return FiberScheduler__spawn_with_stack_size(self, entry, argument, self->default_stack_size);
}

Result(FiberPtr) FiberScheduler__spawn_with_stack_size(FiberSchedulerPtr self, FiberEntry entry, Anything argument,
                                                       size_t stack_size)
{
    if (self->live_count >= self->max_fibers || self->live_count >= FIBER_SCHEDULER_MAX_FIBERS)
    {
        return Error(FiberPtr, fiber_limit_reached, FIBER_LIMIT_REACHED);
    }

    match_result(
        Fiber__heap_new_with_stack_size(entry, argument, stack_size),
        success(fiber)
        {
            match_result(
                Fiber__mark_ready(fiber),
                success_do_nothing(),
                error(code, message)
                {
                    Fiber__dispose(fiber);
                    return Error(FiberPtr, message, code);
                }
            );

            match_result(
                FiberQueue__push(&self->ready, fiber),
                success_do_nothing(),
                error(code, message)
                {
                    Fiber__dispose(fiber);
                    return Error(FiberPtr, queue_full, FIBER_QUEUE_FULL);
                }
            );

            self->live_count++;
            return Success(FiberPtr, fiber);
        },
        error(code, message)
        {
            return Error(FiberPtr, message, code);
        }
    );

    return Error(FiberPtr, queue_full, FIBER_QUEUE_FULL);
}

static Result(FiberCode)
    FiberScheduler__complete_resumed_fiber(FiberSchedulerPtr self, FiberPtr fiber, ConstResultPtr(FiberCode) result)
{
    if (Fiber__state(fiber) == FIBER_DONE)
    {
        self->current = None(FiberPtr);
        Fiber__dispose(fiber);
        self->live_count--;
    }
    else if (Fiber__state(fiber) != FIBER_WAITING)
    {
        self->current = None(FiberPtr);
    }

    return *result;
}

Result(FiberCode) FiberScheduler__await_current(FiberSchedulerPtr self, FiberWaitKey key)
{
    match_optional(
        self->current,
        some(fiber)
        {
            match_result(
                Fiber__mark_waiting(fiber, &key),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(FiberCode, message, code);
                }
            );

            match_result(
                FiberWaiters__push(&self->waiting, fiber),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(FiberCode, queue_full, FIBER_QUEUE_FULL);
                }
            );

            self->current = None(FiberPtr);
            return Fiber__yield_to_scheduler(fiber);
        },
        none()
        {
            return Error(FiberCode, no_current_fiber, FIBER_NO_CURRENT_FIBER);
        }
    );

    return Error(FiberCode, no_current_fiber, FIBER_NO_CURRENT_FIBER);
}

Result(FiberCode) FiberScheduler__await_channel_signal(FiberSchedulerPtr self, ChannelSignalPtr channel)
{
    return FiberScheduler__await_current(self, FiberWaitKey__channel_signal(channel));
}

static bool Fiber__is_waiting_for(FiberPtr * fiber, void * context)
{
    FiberWaitKeyPtr key = context;
    bool result = false;

    match_optional(
        Fiber__wait_key(*fiber),
        some(wait_key)
        {
            result = FiberWaitKey__equals(&wait_key, key);
        },
        none_do_nothing()
    );

    return result;
}

Result(FiberCode) FiberScheduler__wake(FiberSchedulerPtr self, FiberWaitKey key, FiberWakeValue value)
{
    match_find_result(
        FiberWaiters__find(&self->waiting, Fiber__is_waiting_for, &key),
        some(fiber)
        {
            match_result(
                FiberWaiters__remove(&self->waiting, fiber),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(FiberCode, message, code);
                }
            );

            match_result(
                Fiber__set_wake_value(fiber, &value),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(FiberCode, message, code);
                }
            );

            match_result(
                Fiber__mark_ready(fiber),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(FiberCode, message, code);
                }
            );

            match_result(
                FiberQueue__push(&self->ready, fiber),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(FiberCode, queue_full, FIBER_QUEUE_FULL);
                }
            );

            return Success(FiberCode, FIBER_OK);
        },
        none()
        {
            return Error(FiberCode, wait_not_found, FIBER_WAIT_NOT_FOUND);
        }
    );

    return Error(FiberCode, wait_not_found, FIBER_WAIT_NOT_FOUND);
}

Result(FiberCode) FiberScheduler__wake_channel_signal(FiberSchedulerPtr self, ChannelSignalPtr channel)
{
    FiberWaitKey key = FiberWaitKey__channel_signal(channel);
    FiberWakeValue value = FiberWakeValue__new(&key, FIBER_OK, ChannelSignal__data_epoch(channel));
    return FiberScheduler__wake(self, key, value);
}

Result(FiberCode) FiberScheduler__run_ready(FiberSchedulerPtr self)
{
    match_optional(
        FiberQueue__pop_front(&self->ready),
        some(fiber)
        {
            self->current = Some(FiberPtr, fiber);
            Result(FiberCode) result = Fiber__resume(fiber, &self->context);
            match_result(
                result,
                success(code)
                {
                    return FiberScheduler__complete_resumed_fiber(self, fiber, &Success(FiberCode, code));
                },
                error(code, message)
                {
                    return FiberScheduler__complete_resumed_fiber(self, fiber, &Error(FiberCode, message, code));
                }
            );
        },
        none()
        {
            return Error(FiberCode, wait_not_found, FIBER_NO_READY_FIBER);
        }
    );

    return Error(FiberCode, wait_not_found, FIBER_NO_READY_FIBER);
}

Result(FiberCode) FiberScheduler__run_ready_until_blocked(FiberSchedulerPtr self)
{
    while (FiberScheduler__ready_count(self) > 0)
    {
        match_result(
            FiberScheduler__run_ready(self),
            success_do_nothing(),
            error(code, message)
            {
                return Error(FiberCode, message, code);
            }
        );
    }

    return Success(FiberCode, FIBER_OK);
}

Optional(FiberPtr) FiberScheduler__current(FiberSchedulerPtr self)
{
    return self->current;
}

size_t FiberScheduler__live_count(FiberSchedulerPtr self)
{
    return self->live_count;
}

size_t FiberScheduler__max_fibers(FiberSchedulerPtr self)
{
    return self->max_fibers;
}

size_t FiberScheduler__ready_count(FiberSchedulerPtr self)
{
    return FiberQueue__len(&self->ready);
}

size_t FiberScheduler__waiting_count(FiberSchedulerPtr self)
{
    return FiberWaiters__len(&self->waiting);
}
