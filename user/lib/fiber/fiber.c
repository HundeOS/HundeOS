#include "fiber.h"
#include "../sys/mem.h"

static BString allocation_failed = bstr("Fiber allocation failed");
static BString invalid_stack_size = bstr("Invalid fiber stack size");
static BString invalid_state_format = bstr("Invalid fiber state %i");
static BString no_scheduler_context = bstr("Fiber has no scheduler context");
static BString no_wake_value = bstr("Fiber has no wake value");

static void Fiber__start(FiberPtr self);

struct Fiber
{
    FiberState state;
    Optional(FiberWaitKey) wait_key;
    Optional(FiberWakeValue) wake_value;
    Result(FiberCode) last_result;
    FiberEntry entry;
    Anything argument;
    FiberContext context;
    Optional(FiberContextPtr) scheduler_context;
    Address stack_base;
    size_t stack_size;
};

FiberWaitKey FiberWaitKey__new(FiberWaitKind kind, uint64_t id)
{
    return (FiberWaitKey){
        .kind = kind,
        .id = id,
    };
}

FiberWaitKey FiberWaitKey__none(void)
{
    return FiberWaitKey__new(FIBER_WAIT_NONE, 0);
}

FiberWaitKey FiberWaitKey__request(uint64_t request_id)
{
    return FiberWaitKey__new(FIBER_WAIT_REQUEST, request_id);
}

FiberWaitKey FiberWaitKey__block_request(uint64_t request_id)
{
    return FiberWaitKey__request(request_id);
}

FiberWaitKey FiberWaitKey__channel_signal(ChannelSignalPtr channel)
{
    return FiberWaitKey__new(FIBER_WAIT_CHANNEL, (uint64_t)ChannelSignal__state(channel));
}

bool FiberWaitKey__equals(FiberWaitKeyPtr self, FiberWaitKeyPtr other)
{
    return self->kind == other->kind && self->id == other->id;
}

FiberWakeValue FiberWakeValue__new(ConstFiberWaitKeyPtr key, FiberCode status, uint64_t value)
{
    return (FiberWakeValue){
        .key = *key,
        .status = status,
        .value = value,
    };
}

static Fiber Fiber__new(FiberEntry entry, Anything argument, Address stack_base, size_t stack_size)
{
    return (Fiber){
        .state = FIBER_CREATED,
        .wait_key = None(FiberWaitKey),
        .wake_value = None(FiberWakeValue),
        .last_result = Success(FiberCode, FIBER_OK),
        .entry = entry,
        .argument = argument,
        .scheduler_context = None(FiberContextPtr),
        .stack_base = stack_base,
        .stack_size = stack_size,
    };
}

static Result(FiberCode) Fiber__invalid_state_error(FiberPtr self)
{
    return Error(FiberCode, BString__sprintf(&invalid_state_format, (int)self->state), FIBER_INVALID_STATE);
}

Result(FiberPtr) Fiber__heap_new(FiberEntry entry, Anything argument)
{
    return Fiber__heap_new_with_stack_size(entry, argument, FIBER_DEFAULT_STACK_SIZE);
}

Result(FiberPtr) Fiber__heap_new_with_stack_size(FiberEntry entry, Anything argument, size_t stack_size)
{
    if (stack_size < FIBER_STACK_ALIGNMENT)
    {
        return Error(FiberPtr, invalid_stack_size, FIBER_INVALID_STACK_SIZE);
    }

    FiberPtr fiber;
    match_result(
        mem_alloc_bytes(sizeof(Fiber)),
        success(allocation) { fiber = (FiberPtr)allocation; },
        error(code, message) { return Error(FiberPtr, allocation_failed, code); }
    );

    void * stack;
    match_result(
        mem_alloc_pages_result(stack_size),
        success(allocation) { stack = (void *)allocation; },
        error(code, message)
        {
            free(fiber);
            return Error(FiberPtr, allocation_failed, code);
        }
    );

    *fiber = Fiber__new(entry, argument, (Address)stack, stack_size);

    match_result(
        FiberContext__init(&fiber->context, Fiber__stack_base(fiber), Fiber__stack_size(fiber), Fiber__start, fiber),
        success_do_nothing(),
        error(code, message)
        {
            mem_free_pages(stack);
            free(fiber);
            return Error(FiberPtr, message, code);
        }
    );

    return Success(FiberPtr, fiber);
}

FiberState Fiber__state(FiberPtr self)
{
    return self->state;
}

Address Fiber__stack_base(FiberPtr self)
{
    return self->stack_base;
}

Address Fiber__stack_top(FiberPtr self)
{
    Address stack_top = self->stack_base + self->stack_size;
    return stack_top & ~((Address)FIBER_STACK_ALIGNMENT - 1);
}

size_t Fiber__stack_size(FiberPtr self)
{
    return self->stack_size;
}

Optional(FiberWaitKey) Fiber__wait_key(FiberPtr self)
{
    return self->wait_key;
}

Optional(FiberWakeValue) Fiber__wake_value(FiberPtr self)
{
    return self->wake_value;
}

Result(FiberWakeValue) Fiber__take_wake_value(FiberPtr self)
{
    match_optional(
        self->wake_value,
        some(value)
        {
            self->wake_value = None(FiberWakeValue);
            return Success(FiberWakeValue, value);
        },
        none() { return Error(FiberWakeValue, no_wake_value, FIBER_INVALID_STATE); }
    );

    return Error(FiberWakeValue, no_wake_value, FIBER_INVALID_STATE);
}

Result(FiberCode) Fiber__mark_ready(FiberPtr self)
{
    if (self->state == FIBER_DONE)
    {
        return Fiber__invalid_state_error(self);
    }

    self->state = FIBER_READY;
    self->wait_key = None(FiberWaitKey);
    return Success(FiberCode, FIBER_OK);
}

Result(FiberCode) Fiber__mark_running(FiberPtr self)
{
    if (self->state != FIBER_READY && self->state != FIBER_CREATED)
    {
        return Fiber__invalid_state_error(self);
    }

    self->state = FIBER_RUNNING;
    return Success(FiberCode, FIBER_OK);
}

Result(FiberCode) Fiber__mark_waiting(FiberPtr self, ConstFiberWaitKeyPtr key)
{
    if (self->state != FIBER_RUNNING)
    {
        return Fiber__invalid_state_error(self);
    }

    self->state = FIBER_WAITING;
    self->wait_key = Some(FiberWaitKey, *key);
    return Success(FiberCode, FIBER_OK);
}

Result(FiberCode) Fiber__set_wake_value(FiberPtr self, ConstFiberWakeValuePtr value)
{
    self->wake_value = Some(FiberWakeValue, *value);
    return Success(FiberCode, FIBER_OK);
}

Result(FiberCode) Fiber__mark_done(FiberPtr self)
{
    self->state = FIBER_DONE;
    self->wait_key = None(FiberWaitKey);
    return Success(FiberCode, FIBER_OK);
}

static Result(FiberCode) Fiber__last_result(FiberPtr self)
{
    return self->last_result;
}

static void Fiber__start(FiberPtr self)
{
    match_result(
        self->entry(self, self->argument),
        success(code) { self->last_result = Success(FiberCode, code); },
        error(code, message) { self->last_result = Error(FiberCode, message, code); }
    );

    if (self->state != FIBER_WAITING)
    {
        match_result(
            Fiber__mark_done(self),
            success_do_nothing(),
            error(code, message) { self->last_result = Error(FiberCode, message, code); }
        );
    }

    Fiber__yield_to_scheduler(self);

    for (;;)
    {
    }
}

Result(FiberCode) Fiber__resume(FiberPtr self, FiberContextPtr scheduler_context)
{
    match_result(
        Fiber__mark_running(self),
        success_do_nothing(),
        error(code, message) { return Error(FiberCode, message, code); }
    );

    self->scheduler_context = Some(FiberContextPtr, scheduler_context);

    FiberContext__switch(scheduler_context, &self->context);
    return Fiber__last_result(self);
}

Result(FiberCode) Fiber__yield_to_scheduler(FiberPtr self)
{
    match_optional(
        self->scheduler_context,
        some(scheduler_context)
        {
            FiberContext__switch(&self->context, scheduler_context);
            return Fiber__last_result(self);
        },
        none() { return Error(FiberCode, no_scheduler_context, FIBER_INVALID_STATE); }
    );

    return Error(FiberCode, no_scheduler_context, FIBER_INVALID_STATE);
}

void Fiber__dispose(FiberPtr self)
{
    mem_free_pages((void *)self->stack_base);
    free(self);
}
