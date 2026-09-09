#include "../../fiber.h"

#include <string.h>

static BString invalid_context_stack = bstr("Invalid fiber context stack");

Result(FiberCode) FiberContext__init(FiberContextPtr self, Address stack_base, size_t stack_size,
                                     FiberContextStart start, FiberPtr fiber)
{
    if (stack_size < FIBER_STACK_ALIGNMENT)
    {
        return Error(FiberCode, invalid_context_stack, FIBER_INVALID_STACK_SIZE);
    }

    memset(self, 0, sizeof(FiberContext));
    Address stack_top = (stack_base + stack_size) & ~((Address)FIBER_STACK_ALIGNMENT - 1);
    self->sp = stack_top;
    self->lr = (uint64_t)start;
    self->x0 = (uint64_t)fiber;

    return Success(FiberCode, FIBER_OK);
}
