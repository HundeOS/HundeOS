#include "../../fiber.h"

#include "memory.h"

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

static BString invalid_context_stack = bstr("Invalid fiber context stack");
static BString no_context_slot = bstr("No hosted fiber context slot");

#define FIBER_CONTEXT_HOSTED_MAX_CONTEXTS 1024

ResultDef(int);

static Optional(FiberContextPtr) fiber_contexts[FIBER_CONTEXT_HOSTED_MAX_CONTEXTS] = {0};

static Result(int) FiberContext__allocate_slot(FiberContextPtr self)
{
    for (int index = 0; index < FIBER_CONTEXT_HOSTED_MAX_CONTEXTS; index++)
    {
        match_optional(
            fiber_contexts[index],
            some_do_nothing(),
            none()
            {
                fiber_contexts[index] = Some(FiberContextPtr, self);
                return Success(int, index);
            }
        );
    }

    return Error(int, no_context_slot, FIBER_ALLOCATION_FAILED);
}

static void FiberContext__start(int slot)
{
    match_optional(fiber_contexts[slot],
        some(self)
        {
            self->start(self->fiber);
        },
        none_do_nothing()
    );
}

Result(FiberCode) FiberContext__init(FiberContextPtr self, Address stack_base, size_t stack_size,
                                     FiberContextStart start, FiberPtr fiber)
{
    if (stack_size < FIBER_STACK_ALIGNMENT)
    {
        return Error(FiberCode, invalid_context_stack, FIBER_INVALID_STACK_SIZE);
    }

    Mem__memset(self, 0, sizeof(FiberContext));
    match_result(
        FiberContext__allocate_slot(self),
        success(slot)
        {
            self->slot = slot;
        },
        error(code, message)
        {
            return Error(FiberCode, message, code);
        }
    );

    if (getcontext(&self->context) != 0)
    {
        return Error(FiberCode, invalid_context_stack, FIBER_INVALID_STACK_SIZE);
    }

    self->context.uc_stack.ss_sp = (void *)stack_base;
    self->context.uc_stack.ss_size = stack_size;
    self->context.uc_link = 0;
    self->start = start;
    self->fiber = fiber;

    makecontext(&self->context, (void (*)(void))FiberContext__start, 1, self->slot);

    return Success(FiberCode, FIBER_OK);
}

void FiberContext__switch(FiberContextPtr self, FiberContextPtr next)
{
    swapcontext(&self->context, &next->context);
}
