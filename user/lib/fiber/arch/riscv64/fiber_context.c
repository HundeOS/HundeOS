#include "../../fiber.h"

#include "memory.h"

static BString invalid_context_stack = bstr("Invalid fiber context stack");

Result(FiberCode) FiberContext__init(FiberContextPtr self, Address stack_base, size_t stack_size,
                                     FiberContextStart start, FiberPtr fiber)
{
    if (stack_size < FIBER_STACK_ALIGNMENT)
    {
        return Error(FiberCode, invalid_context_stack, FIBER_INVALID_STACK_SIZE);
    }

    Mem__memset(self, 0, sizeof(FiberContext));
    self->sp = (stack_base + stack_size) & ~((Address)FIBER_STACK_ALIGNMENT - 1);
    self->ra = (uint64_t)start;
    self->a0 = (uint64_t)fiber;

    return Success(FiberCode, FIBER_OK);
}
